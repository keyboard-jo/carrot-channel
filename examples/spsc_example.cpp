#include <iostream>
#include <thread>
#include <chrono>
#include <carrot/spsc.hpp>

int main() {
    auto [tx, rx] = carrot::make_channel<int, 1024>();

    std::thread producer([tx = std::move(tx)]() mutable {
        for (int i = 0; i < 100; ++i) {
            while (!tx.send(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([rx = std::move(rx)]() mutable {
        int val;
        int received_count = 0;
        while (received_count < 100) {
            if (rx.recv(val)) {
                std::cout << "Received: " << val << "\n";
                ++received_count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    return 0;
}