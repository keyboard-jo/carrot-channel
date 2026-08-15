#pragma once

#include <cstddef>
#include <array>
#include <bit>
#include <atomic>
#include <new>
#include <memory>
#include <utility>
#include <optional>
#include <concepts>
#include <thread>


namespace carrot {
    template<typename P>
    concept full_policy = requires {
        { P::block_on_full } -> std::convertible_to<bool>;
    };
    struct drop_policy {
        static constexpr bool block_on_full = false;
    };

    struct spin_policy {
        static constexpr bool block_on_full = true;
    };

    template<typename P>
    concept empty_policy = requires {
        { P::block_on_empty } -> std::convertible_to<bool>;
    };

    struct poll_policy {
        static constexpr bool block_on_empty = false;
    };

    struct wait_policy {
        static constexpr bool block_on_empty = true;
    };

    static_assert(full_policy<drop_policy>);
    static_assert(full_policy<spin_policy>);
    static_assert(empty_policy<poll_policy>);
    static_assert(empty_policy<wait_policy>);

    template<typename C, typename T>
    concept batch_container = requires(C c, T item) {
        { c.push_back(std::move(item)) };
    };

    template<typename T, std::size_t Capacity, full_policy FullPolicy, empty_policy EmptyPolicy> class sender;
    template<typename T, std::size_t Capacity, full_policy FullPolicy, empty_policy EmptyPolicy> class receiver;
    template<typename data_T, std::size_t Capacity, full_policy FullPolicy> class spsc_queue;

    template<typename T, std::size_t Capacity, full_policy FullPolicy, empty_policy EmptyPolicy>
    auto make_channel() -> std::pair<sender<T, Capacity, FullPolicy, EmptyPolicy>, receiver<T, Capacity, FullPolicy, EmptyPolicy>>;

    template<typename data_T, std::size_t Capacity, full_policy FullPolicy = drop_policy>
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
            while (true) {
                const auto current_write = m_write_idx.load(std::memory_order_relaxed);
                const auto current_read = m_read_idx.load(std::memory_order_acquire);
    
                if (current_write - current_read >= Capacity) {
                    if constexpr (FullPolicy::block_on_full) {
                        std::this_thread::yield();
                        continue;
                    } else {
                        m_dropped_count.fetch_add(1, std::memory_order_relaxed);
                        return false;
                    }
                }
    
                const auto slot = current_write & m_mask;
                ::new (static_cast<void*>(m_cells[slot].m_buffer)) data_T(std::move(element));
    
                m_write_idx.store(current_write + 1, std::memory_order_release);
                return true;
            }
        }

        template<typename InputIt>
        requires std::forward_iterator<InputIt> 
                && std::is_constructible_v<data_T, std::iter_reference_t<InputIt>>
        auto enqueue_bulk(InputIt first, InputIt last) -> std::size_t {
            const auto count = std::distance(first, last);
            if (count <= 0) return 0;
            const auto ucount = static_cast<std::size_t>(count);

            while (true) {
                const auto current_write = m_write_idx.load(std::memory_order_relaxed);
                const auto current_read = m_read_idx.load(std::memory_order_acquire);

                const auto available_space = Capacity - (current_write - current_read);

                if (ucount > available_space) {
                    if constexpr (FullPolicy::block_on_full) {
                        std::this_thread::yield();
                        continue;
                    } else {
                        m_dropped_count.fetch_add(ucount, std::memory_order_relaxed);
                        return false;
                    }
                }

                for (std::size_t i = 0; i < ucount; ++i) {
                    const auto slot = (current_write + i) & m_mask;
                    ::new (static_cast<void*>(m_cells[slot].m_buffer)) data_T(std::move(*first));
                    ++first;
                }

                m_write_idx.store(current_write + ucount, std::memory_order_release);
                return ucount;

            }
        }

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
        }

        template<typename Container>
        requires batch_container<Container, data_T>
        auto dequeue_bulk(Container& container, std::size_t max_count) -> std::size_t {
            const auto current_read = m_read_idx.load(std::memory_order_relaxed);
            const auto current_write = m_write_idx.load(std::memory_order_acquire);

            if (current_read == current_write || max_count == 0) {
                return 0;
            }

            const std::size_t available = current_write - current_read;
            const std::size_t batch_size = std::min(available, max_count);

            for (std::size_t i = 0; i < batch_size; ++i) {
                const auto slot = (current_read + i) & m_mask;
                auto* ptr = reinterpret_cast<data_T*>(m_cells[slot].m_buffer);
                container.push_back(std::move(*ptr));
                ptr->~data_T();
            }

            m_read_idx.store(current_read + batch_size, std::memory_order_release);
            return batch_size;
        }

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
        }

        auto dropped_count() const noexcept -> std::size_t {
            return m_dropped_count.load(std::memory_order_relaxed);
        }

    private:
        struct cell_t {
            alignas(data_T) std::byte m_buffer[sizeof(data_T)];
        };

        static constexpr std::size_t m_mask = Capacity - 1;

        #ifdef __cpp_lib_hardware_interference_size
            static constexpr std::size_t cacheline_size = std::hardware_destructive_interference_size;
        #else
            static constexpr std::size_t cacheline_size = 64; // Safe standard fallback for x86/ARM
        #endif

        alignas(cacheline_size) std::atomic_size_t m_write_idx{0};
        alignas(cacheline_size) std::atomic_size_t m_read_idx{0};
        alignas(cacheline_size) std::atomic_size_t m_dropped_count{0};
        alignas(cacheline_size) std::array<cell_t, Capacity> m_cells;
    };

    template<typename T, std::size_t Capacity, full_policy FullPolicy, empty_policy EmptyPolicy>
    class sender {
    public:
        explicit sender(std::shared_ptr<spsc_queue<T, Capacity, FullPolicy>> queue)
            : m_queue(std::move(queue)) {}

        sender(const sender&) = delete;
        sender& operator=(const sender&) = delete;
        sender(sender&&) = default;
        sender& operator=(sender&&) = default;

        auto send(T element) -> bool{
            if (!m_queue) return false;
            return m_queue->enqueue(std::move(element));
        }

        template<typename InputIt>
        requires std::forward_iterator<InputIt> 
                && std::is_constructible_v<T, std::iter_reference_t<InputIt>>
        auto send_bulk(InputIt first, InputIt last) -> std::size_t {
            if (!m_queue) return 0;
            return m_queue->enqueue_bulk(first, last);
        }

        auto dropped_count() const noexcept -> std::size_t {
            return m_queue ? m_queue->dropped_count() : 0;
        }

    private:
        std::shared_ptr<spsc_queue<T, Capacity>> m_queue;
    };

    template<typename T, std::size_t Capacity, full_policy FullPolicy, empty_policy EmptyPolicy>
    class receiver {
    public:
        explicit receiver(std::shared_ptr<spsc_queue<T, Capacity, FullPolicy>> queue)
            : m_queue(std::move(queue)) {}

        receiver(const receiver&) = delete;
        receiver& operator=(const receiver&) = delete;
        receiver(receiver&&) = default;
        receiver& operator=(receiver&&) = default;

        auto recv(T& element_out) -> bool {
            if (!m_queue) return false;
            while (true) {
                if (m_queue->try_dequeue(element_out)) {
                    return true;
                }
                if constexpr (EmptyPolicy::block_on_empty) {
                    std::this_thread::yield();
                    continue;
                } else {
                    return false;
                }
            }
        }

        auto recv() -> std::optional<T> {
            T val;
            if (m_queue && m_queue->try_dequeue(val)) {
                return val;
            }
            return std::nullopt;
        }

        template<typename Container>
        requires batch_container<Container, T>
        auto recv_bulk(Container& container, std::size_t max_count) -> std::size_t {
            if (!m_queue) return 0;
            while (true) {
                const std::size_t fetched = m_queue->dequeue_bulk(container, max_count);
                if (fetched > 0 || !EmptyPolicy::block_on_empty) {
                    return fetched;
                }
                std::this_thread::yield();
            }
        }
    private:
        std::shared_ptr<spsc_queue<T, Capacity>> m_queue;
    };

    template<typename T, std::size_t Capacity, full_policy FullPolicy, empty_policy EmptyPolicy>
    auto make_channel() -> std::pair<sender<T, Capacity, FullPolicy, EmptyPolicy>, receiver<T, Capacity, FullPolicy, EmptyPolicy>> {
        auto queue = std::make_shared<spsc_queue<T, Capacity, FullPolicy>>();
        return { 
            sender<T, Capacity, FullPolicy, EmptyPolicy>(queue), 
            receiver<T, Capacity, FullPolicy, EmptyPolicy>(queue) 
        };
    }
}