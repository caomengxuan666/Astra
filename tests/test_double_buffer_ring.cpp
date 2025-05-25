#include "core/astra.hpp"
#include "datastructures/double_buffer_ring.hpp"
#include <atomic>
#include <gtest/gtest.h>
#include <thread>


using namespace Astra::datastructures;

// 使用 1024 容量的双缓冲区
constexpr size_t kBufferSize = 1024;
using TestBuffer = LockFreeDoubleBuffer<int, kBufferSize>;

TEST(DoubleBufferTest, SingleProducerSingleConsumer_SPSC) {
    TestBuffer buffer;
    constexpr int NumItems = 10000;
    std::atomic<bool> done(false);

    // 生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < NumItems; ++i) {
            while (!buffer.Push(i)) {
                std::this_thread::yield();
            }
        }
        done = true;
    });

    // 消费者线程
    std::atomic<int> consumer_count(0);
    std::thread consumer([&]() {
        int val;
        while (!done || consumer_count < NumItems) {
            if (buffer.Pop(val)) {
                EXPECT_EQ(val, consumer_count.load());
                consumer_count.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumer_count.load(), NumItems);
}

// 多次运行以验证稳定性
TEST(DoubleBufferTest, MultipleRuns_StressTest) {
    for (int run = 0; run < 10; ++run) {
        TestBuffer buffer;
        std::atomic<bool> done(false);

        std::thread producer([&]() {
            for (int i = 0; i < 10000; ++i) {
                while (!buffer.Push(i)) {
                    std::this_thread::yield();
                }
            }
            done = true;
        });

        std::atomic<int> count(0);
        std::thread consumer([&]() {
            int val;
            while (count.load() < 10000 || !done.load()) {
                if (buffer.Pop(val)) {
                    ++count;
                } else {
                    std::this_thread::yield();
                }
            }
        });

        producer.join();
        consumer.join();

        EXPECT_EQ(count.load(), 10000);
    }
}