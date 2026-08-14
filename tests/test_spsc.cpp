#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cassert>
#include <carrot/spsc.hpp>

void test_basic() {
    carrot::spsc_queue<int, 4> q;
    int val = 0;

    assert(!q.try_dequeue(val));

    assert(q.enqueue(10));
    assert(q.enqueue(20));

    assert(q.try_dequeue(val));
    assert(val == 10);

    assert(q.try_dequeue(val));
    assert(val == 20);

    assert(!q.try_dequeue(val));
    std::cout << "[Test] test_basic passed.\n";
}

void test_capacity_and_wrap_around() {
    carrot::spsc_queue<int, 4> q;

    assert(q.enqueue(1));
    assert(q.enqueue(2));
    assert(q.enqueue(3));
    assert(q.enqueue(4));
    assert(!q.enqueue(5));

    int val = 0;

    assert(q.try_dequeue(val) && val == 1);
    assert(q.enqueue(5));
    assert(!q.enqueue(6));

    assert(q.try_dequeue(val) && val == 2);
    assert(q.try_dequeue(val) && val == 3);
    assert(q.try_dequeue(val) && val == 4);
    assert(q.try_dequeue(val) && val == 5);
    assert(!q.try_dequeue(val));

    assert(q.dropped_count() == 2);

    std::cout << "[Test] test_capacity_and_wrap_around passed.\n";
}

void test_try_discard() {
    carrot::spsc_queue<int, 4> q;
    assert(q.enqueue(100));
    assert(q.try_discard());

    int val = 0;
    assert(!q.try_dequeue(val));

    std::cout << "[Test] test_try_discard passed.\n";
}

void test_bulk_operations() {
    auto [tx, rx] = carrot::make_channel<int, 16, carrot::drop_policy, carrot::poll_policy>();

    std::vector<int> input = {10, 20, 30, 40, 50};
    std::size_t sent = tx.send_bulk(input.begin(), input.end());
    assert(sent == 5);

    std::vector<int> output;
    output.reserve(10);
    std::size_t received = rx.recv_bulk(output, 10);

    assert(received == 5);
    assert(output == input);

    std::cout << "[Test] test_bulk_operations passed.\n";
}

void test_wait_policy() {
    // Using wait_policy so the consumer automatically yields/waits when empty
    auto [tx, rx] = carrot::make_channel<int, 8, carrot::drop_policy, carrot::wait_policy>();

    std::thread consumer([rx = std::move(rx)]() mutable {
        int val = 0;
        // This will block/wait safely until the producer sends the item
        assert(rx.recv(val));
        assert(val == 999);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(tx.send(999));

    consumer.join();
    std::cout << "[Test] test_wait_policy passed.\n";
}

void test_concurrency() {
    constexpr int num_items = 100000;
    auto [tx, rx] = carrot::make_channel<int, 1024, carrot::drop_policy, carrot::wait_policy>();

    std::thread producer([tx = std::move(tx)]() mutable {
        for (int i = 0; i < num_items; ++i) {
            while (!tx.send(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([rx = std::move(rx)]() mutable {
        int expected = 0;
        while (expected < num_items) {
            int val = 0;
            if (rx.recv(val)) {
                assert(val == expected);
                ++expected;
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "[Test] test_concurrency passed.\n";
}

int main() {
    std::cout << "Starting SPSC Queue Tests...\n";
    test_basic();
    test_capacity_and_wrap_around();
    test_try_discard();
    test_bulk_operations();
    test_wait_policy();
    test_concurrency();
    std::cout << "All SPSC Queue Tests Passed Successfully!\n";
    return 0;
}