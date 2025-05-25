#include "core/astra.hpp"
#include <datastructures/ring_buffer.hpp>
#include <gtest/gtest.h>
#include <thread>

using namespace Astra::datastructures;

TEST(RingBufferTest, BasicPushPop) {
    RingBuffer<int, 4> buffer;

    EXPECT_TRUE(buffer.Push(1));
    EXPECT_TRUE(buffer.Push(2));
    EXPECT_TRUE(buffer.Push(3));
    EXPECT_FALSE(buffer.Push(4));// full

    int val;
    EXPECT_TRUE(buffer.Pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(buffer.Pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(buffer.Pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_FALSE(buffer.Pop(val));// empty
}

TEST(RingBufferTest, SPSC_MultiThreaded) {
    constexpr size_t NumItems = 10000;
    RingBuffer<int, NumItems + 1> buffer;

    std::atomic<bool> done{false};
    std::atomic<int> consumer_count{0};

    std::thread producer([&]() {
        for (int i = 0; i < static_cast<int>(NumItems); ++i) {
            while (!buffer.Push(i)) {
                std::this_thread::yield();
            }
        }
        done = true;
    });

    std::thread consumer([&]() {
        int count = 0;
        int expected = 0;

        while (!done || !buffer.Empty()) {
            int val;
            if (buffer.Pop(val)) {
                EXPECT_EQ(val, expected++);
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
        consumer_count.store(count);
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumer_count.load(), NumItems);
}