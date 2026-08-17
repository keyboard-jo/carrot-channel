# Carrot Channel (`carrot-channel`)

A high-performance, header-only C++20 lock-free Single-Producer Single-Consumer (SPSC) channel library with configurable backpressure and blocking policies.

---

## Features

* **Lock-Free Ring Buffer**: Utilizes atomic acquire-release semantics for low-latency thread communication without mutexes.
* **C++20 Concepts & Safety**: Built with modern C++20 features, utilizing concepts for policies, iterator constraints, and batch containers.
* **Flexible Policies**:
* **Full Policies**: Choose between dropping messages (`drop_policy`) or yielding/spinning (`spin_policy`) when the buffer is full.
* **Empty Policies**: Choose between non-blocking polling (`poll_policy`) or blocking/yielding (`wait_policy`) when the buffer is empty.


* **Bulk Operations**: Optimized bulk enqueue (`send_bulk`) and dequeue (`recv_bulk`) routines for high-throughput batching.
* **False Sharing Mitigation**: Cache-line aligned atomic indices and cell buffers using `std::hardware_destructive_interference_size`.
* **RAII Lifecycle Safety**: Proper manual lifetime management (placement `new` and explicit destructors) for arbitrary non-trivial types.

---

## Requirements

* A compiler supporting **C++20** (e.g., GCC 11+, Clang 13+, MSVC 2022+).

---

## Installation

`carrot-channel` is a header-only library. You can integrate it into your project either by manually copying the header or by using CMake FetchContent.

### Method 1: Manual Header Copy

Simply copy `spsc.hpp` into your project's include path (e.g., inside a `carrot/` directory) and include it:

```cpp
#include <carrot/spsc.hpp>

```

### Method 2: CMake FetchContent

You can pull `carrot-channel` directly into your CMake project using `FetchContent`:

```cmake
include(FetchContent)

FetchContent_Declare(
    carrot-spsc
    GIT_REPOSITORY https://github.com/keyboard-jo/carrot-channel.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(carrot-spsc)

```

---

## Quick Start

Here is a complete example demonstrating a producer-consumer setup with mixed bulk and single operations:

```cpp
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <carrot/spsc.hpp>

int main() {
    // Create a channel with capacity 1024, dropping policy on full, and polling on empty
    auto [tx, rx] = carrot::make_channel<int, 1024, carrot::drop_policy, carrot::poll_policy>();

    // Producer thread using both bulk and single operations
    std::thread producer([tx = std::move(tx)]() mutable {
        // 1. Bulk sending using a std::vector
        std::vector<int> initial_batch = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        std::size_t sent = tx.send_bulk(initial_batch.begin(), initial_batch.end());
        std::cout << "[Producer] Sent bulk batch of " << sent << " items.\n";

        // 2. Send the rest using single items up to 100
        for (int i = 11; i <= 100; ++i) {
            while (!tx.send(i)) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread using bulk receiving
    std::thread consumer([rx = std::move(rx)]() mutable {
        int received_count = 0;
        std::vector<int> batch_out;
        batch_out.reserve(50); // Pre-allocate to avoid allocations on the hot path

        while (received_count < 100) {
            batch_out.clear();
            
            // Drains up to 50 items in a single atomic sweep
            std::size_t fetched = rx.recv_bulk(batch_out, 50);

            if (fetched > 0) {
                for (int val : batch_out) {
                    std::cout << "[Consumer] Received: " << val << "\n";
                    ++received_count;
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // Check final drop metrics
    std::cout << "[Stats] Total dropped messages: " << tx.dropped_count() << "\n";

    return 0;
}

```

---

## API Reference

### Channel Creation

```cpp
auto [tx, rx] = carrot::make_channel<T, Capacity, FullPolicy, EmptyPolicy>();

```

* `T`: The type of elements stored in the channel.
* `Capacity`: Must be at least `2` and a power of `2`.
* `FullPolicy`:
* `carrot::drop_policy`: Returns `false` on overflow and increments the dropped message counter.
* `carrot::spin_policy`: Yields execution using `std::this_thread::yield()` until space frees up.


* `EmptyPolicy`:
* `carrot::poll_policy`: Returns `false` or `std::nullopt` immediately if empty.
* `carrot::wait_policy`: Yields execution until items become available.



### Sender (`sender<T, Capacity, EmptyPolicy FullPolicy,>`)

* `bool send(T element)`: Enqueues a single element.
* `std::size_t send_bulk(InputIt first, InputIt last)`: Enqueues a range of elements via forward iterators.
* `std::size_t dropped_count() const noexcept`: Returns the total number of dropped messages (if using `drop_policy`).

### Receiver (`receiver<T, Capacity, EmptyPolicy FullPolicy,>`)

* `bool recv(T& element_out)`: Receives a single element into a reference.
* `std::optional<T>`: Receives a single element wrapped in an `std::optional`.
* `std::size_t recv_bulk(Container& container, std::size_t max_count)`: Dequeues up to `max_count` items directly into any container satisfying the `batch_container` concept (e.g., `std::vector<T>`).

---

## Roadmap & Work in Progress

While `carrot-channel` currently provides a high-performance **SPSC** (Single-Producer Single-Consumer) channel, future releases will expand support for alternative concurrency patterns:

* **MPSC (Multi-Producer Single-Consumer)**: Allowing multiple threads to safely push items concurrently into a single shared queue.
* **SPMC (Single-Producer Multi-Consumer)**: Enabling one producer to broadcast or distribute work items across multiple worker threads.
* **MPMC (Multi-Producer Multi-Consumer)**: Fully generalized lock-free ring buffers supporting concurrent producers and consumers.

---

## Design & Performance

* **Memory Ordering**: Leverages `std::memory_order_acquire` and `std::memory_order_release` to enforce synchronization between the producer and consumer threads with minimal synchronization overhead.
* **Zero Heap Allocation in Queue**: The backing ring-buffer cells are allocated inline within the `spsc_queue` array structure.