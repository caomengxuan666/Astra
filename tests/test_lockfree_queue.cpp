#include "core/astra.hpp"
#include <datastructures/lockfree_queue.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace Astra::datastructures;

// 测试基本的入队出队功能
TEST(LockFreeQueueTest, BasicPushPop) {
    LockFreeQueue<int, 4> queue;

    EXPECT_TRUE(queue.Push(1));
    EXPECT_TRUE(queue.Push(2));
    EXPECT_TRUE(queue.Push(3));

    int val;
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_FALSE(queue.Pop(val));// 队列空
}

// 测试队列满时 Push 返回 false
TEST(LockFreeQueueTest, FullQueue) {
    LockFreeQueue<int, 4> queue;

    EXPECT_TRUE(queue.Push(1));
    EXPECT_TRUE(queue.Push(2));
    EXPECT_TRUE(queue.Push(3));
    EXPECT_TRUE(queue.Push(4)); // 容量是 4，此时队列已满
    EXPECT_FALSE(queue.Push(5));// 再 Push 应该失败

    // 出队一个元素后可以继续入队
    int val;
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(queue.Push(5));
}

// 多线程并发测试（生产者-消费者）
TEST(LockFreeQueueTest, MultiThreadedPushPop) {
    constexpr size_t NumItems = 10000;

    // ✅ 模板参数指定容量
    LockFreeQueue<int, NumItems + 1> queue;

    std::atomic<bool> done{false};
    std::atomic<int> consumer_count{0};

    std::thread consumer([&]() {
        int val;
        while (!done || !queue.Empty()) {
            if (queue.Pop(val)) {
                ++consumer_count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread producer([&]() {
        for (int i = 0; i < static_cast<int>(NumItems); ++i) {
            while (!queue.Push(i)) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    done = true;
    consumer.join();

    EXPECT_EQ(consumer_count.load(), NumItems);
}
// 测试多个消费者线程并发消费
TEST(LockFreeQueueTest, MultiConsumer) {
    constexpr size_t NumItems = 10000;
    LockFreeQueue<int, NumItems + 1> queue;
    std::atomic<int> total_consumed{0};

    std::thread producer([&]() {
        for (int i = 0; i < static_cast<int>(NumItems); ++i) {
            while (!queue.Push(i)) {
                std::this_thread::yield();
            }
        }
    });

    const size_t NumConsumers = 4;
    std::vector<std::thread> consumers;
    std::atomic<bool> done{false};

    for (size_t i = 0; i < NumConsumers; ++i) {
        consumers.emplace_back([&]() {
            int val;
            while (!done || !queue.Empty()) {
                if (queue.Pop(val)) {
                    ++total_consumed;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    producer.join();
    done = true;

    for (auto &t: consumers) {
        t.join();
    }

    EXPECT_EQ(total_consumed.load(), NumItems);
}

// 测试容量限制和循环行为
TEST(LockFreeQueueTest, CircularBehavior) {
    LockFreeQueue<int, 4> queue;

    EXPECT_TRUE(queue.Push(1));
    EXPECT_TRUE(queue.Push(2));
    EXPECT_TRUE(queue.Push(3));
    EXPECT_TRUE(queue.Push(4)); // 这里 Push 成功
    EXPECT_FALSE(queue.Push(5));// 满了

    int val;
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(queue.Push(5)); // Push 成功
    EXPECT_FALSE(queue.Push(6));// Push 失败 ✅

    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 4);
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 5);
    EXPECT_FALSE(queue.Pop(val));// 空了
}