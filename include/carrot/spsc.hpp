#pragma once

#include <cstddef>
#include <array>
#include <bit>
#include <atomic>
#include <new>
#include <memory>
#include <utility>
#include <optional>


namespace carrot {
    template<typename T, std::size_t Capacity> class sender;
    template<typename T, std::size_t Capacity> class receiver;
    template<typename data_T, std::size_t Capacity> class spsc_queue;

    template<typename T, std::size_t Capacity>
    auto make_channel() -> std::pair<sender<T, Capacity>, receiver<T, Capacity>>;

    template<typename data_T, std::size_t Capacity>
    class spsc_queue {
    public:
        static_assert(Capacity >= 2, "Queue capacity must be at least 2");
        static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");

        spsc_queue() = default;
        ~spsc_queue() {
            data_T dummy;
            while (try_dequeue(dummy)) {}
        }

        spsc_queue(const spsc_queue&) = delete;
        spsc_queue& operator=(const spsc_queue&) = delete;
        spsc_queue(spsc_queue&&) = delete;
        spsc_queue& operator=(spsc_queue&&) = delete;

        auto enqueue(data_T element) -> bool {
            const auto current_write = m_write_idx.load(std::memory_order_relaxed);
            const auto current_read = m_read_idx.load(std::memory_order_acquire);

            if (current_write - current_read >= Capacity) {
                return false;
            }

            const auto slot = current_write & m_mask;
            ::new (static_cast<void*>(m_cells[slot].m_buffer)) data_T(std::move(element));

            m_write_idx.store(current_write + 1, std::memory_order_release);
            return true;

        };

        auto try_dequeue(data_T& element_out) -> bool {
            const auto current_read = m_read_idx.load(std::memory_order_relaxed);
            const auto current_write = m_write_idx.load(std::memory_order_acquire);

            if (current_read == current_write) {
                return false;
            }

            const auto slot = current_read & m_mask;
            auto* ptr = reinterpret_cast<data_T*>(m_cells[slot].m_buffer);
            element_out = std::move(*ptr);
            ptr->~data_T();

            m_read_idx.store(current_read + 1, std::memory_order_release);
            return true;
        };

        auto try_discard() -> bool {
            const auto current_read = m_read_idx.load(std::memory_order_relaxed);
            const auto current_write = m_write_idx.load(std::memory_order_acquire);

            if (current_read == current_write) {
                return false;
            }

            const auto slot = current_read & m_mask;
            auto* ptr = reinterpret_cast<data_T*>(m_cells[slot].m_buffer);
            ptr->~data_T();

            m_read_idx.store(current_read + 1, std::memory_order_release);
            return true;
        };

    private:
        struct cell_t {
            alignas(data_T) std::byte m_buffer[sizeof(data_T)];
        };

        static constexpr std::size_t m_mask = Capacity - 1;
        static constexpr std::size_t cacheline_size = 64;

        alignas(cacheline_size) std::atomic_size_t m_write_idx{0};
        alignas(cacheline_size) std::atomic_size_t m_read_idx{0};
        alignas(cacheline_size) std::array<cell_t, Capacity> m_cells;
    };

    template<typename T, std::size_t Capacity>
    class sender {
    public:
        explicit sender(std::shared_ptr<spsc_queue<T, Capacity>> queue)
            : m_queue(std::move(queue)) {}

        sender(const sender&) = delete;
        sender& operator=(const sender&) = delete;
        sender(sender&&) = default;
        sender& operator=(sender&&) = default;

        auto send(T element) -> bool{
            if (!m_queue) return false;
            return m_queue->enqueue(std::move(element));
        }

    private:
        std::shared_ptr<spsc_queue<T, Capacity>> m_queue;
    };

    template<typename T, std::size_t Capacity>
    class receiver {
    public:
        explicit receiver(std::shared_ptr<spsc_queue<T, Capacity>> queue)
            : m_queue(std::move(queue)) {}

        receiver(const receiver&) = delete;
        receiver& operator=(const receiver&) = delete;
        receiver(receiver&&) = default;
        receiver& operator=(receiver&&) = default;

        auto recv(T& element_out) -> bool {
            if (!m_queue) return false;
            return m_queue->try_dequeue(element_out);
        }

        auto recv() -> std::optional<T> {
            T val;
            if (m_queue && m_queue->try_dequeue(val)) {
                return val;
            }
            return std::nullopt;
        }
    private:
        std::shared_ptr<spsc_queue<T, Capacity>> m_queue;
    };

    template<typename T, std::size_t Capacity>
    auto make_channel() -> std::pair<sender<T, Capacity>, receiver<T, Capacity>> {
        auto queue = std::make_shared<spsc_queue<T, Capacity>>();
        return { sender<T, Capacity>(queue), receiver<T, Capacity>(queue) };
    }
}