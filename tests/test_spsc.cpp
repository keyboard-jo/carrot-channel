#include <iostream>
#include <thread>
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

    int val  = 0;

    assert(q.try_dequeue(val) && val == 1);
    assert(q.enqueue(5));
    assert(!q.enqueue(6));

    assert(q.try_dequeue(val) && val == 2);
    assert(q.try_dequeue(val) && val == 3);
    assert(q.try_dequeue(val) && val == 4);
    assert(q.try_dequeue(val) && val == 5);
    assert(!q.try_dequeue(val));

    std::cout << "[Test] test_capcity_and_wrap_around passed. \n";
}

void test_try_discard() {
    carrot::spsc_queue<int, 4> q;
    assert(q.enqueue(100));
    assert(q.try_discard());

    int val = 0;
    assert(!q.try_dequeue(val));

    std::cout << "[Test] test_try_discard passed. \n";
}

void test_concurrency() {
    constexpr int num_items = 100000;
    carrot::spsc_queue<int, 1024> q;

    std::thread producer([&q]() {
        for (int i{}; i < num_items; ++i) {
            while (!q.enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&q]() {
        int expected = 0;
        while (expected < num_items) {
            int val = 0;
            if (q.try_dequeue(val)) {
                assert(val == expected);
                ++expected;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "[Test] test_concurrency passes. \n";
}

int main() {
    std::cout << "Starting SPSC Queue Tests...\n";
    test_basic();
    test_capacity_and_wrap_around();
    test_try_discard();
    test_concurrency();
    std::cout << "All SPSC Queue Test Passed Successfully!\n";
    return 0;
}