#include <iostream>
#include <thread>
#include <chrono>
#include <carrot/spsc.hpp>

int main() {
    // Create a queue with a capacity of 8 (must be a power of 2)
    carrot::spsc_queue<int, 8> queue;

    std::cout << "[Main] Starting Producer-Consumer test...\n";

    // Producer Thread
    std::thread producer([&queue]() {
        for (int i = 1; i <= 10; ++i) {
            // Keep trying until enqueue succeeds (queue might be full temporarily)
            while (!queue.enqueue(i)) {
                std::this_thread::yield(); // Yield CPU slice if full
            }
            std::cout << "[Producer] Enqueued: " << i << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate work
        }
        std::cout << "[Producer] Finished producing.\n";
    });

    // Consumer Thread
    std::thread consumer([&queue]() {
        int consumed_count = 0;
        int value = 0;

        // Consume 10 items total
        while (consumed_count < 10) {
            if (queue.try_dequeue(value)) {
                std::cout << "[Consumer] Dequeued: " << value << "\n";
                consumed_count++;
            } else {
                std::this_thread::yield(); // Yield CPU slice if empty
            }
        }
        std::cout << "[Consumer] Finished consuming.\n";
    });

    // Wait for both threads to finish
    producer.join();
    consumer.join();

    std::cout << "[Main] Test completed successfully!\n";
    return 0;
}