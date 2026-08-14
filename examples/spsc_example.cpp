#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <carrot/spsc.hpp>

int main() {
    auto [tx, rx] = carrot::make_channel<int, 1024, carrot::drop_policy, carrot::poll_policy>();

    // Producer thread using both bulk and single operations
    std::thread producer([tx = std::move(tx)]() mutable {
        // 1. Demonstrate bulk sending using a std::vector
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
        batch_out.reserve(50); // Pre-allocate to avoid allocations on hot path

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