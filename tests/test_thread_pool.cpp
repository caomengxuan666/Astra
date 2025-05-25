#include "core/astra.hpp"
#include <atomic>
#include <chrono>
#include <concurrent/thread_pool.hpp>
#include <gtest/gtest.h>


using namespace Astra::concurrent;

TEST(ThreadPoolTest, BasicSubmitAndGet) {
    ThreadPool pool(4);
    auto fut = pool.Submit([](int x) { return x * x; }, 5);
    EXPECT_EQ(fut.get(), 25);
}

TEST(ThreadPoolTest, PriorityBasedExecution) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    auto res = pool.SubmitWithPriority(10, [&counter] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        counter.fetch_add(1);
    });
    EXPECT_TRUE(res.valid());

    res = pool.SubmitWithPriority(1, [&counter] {
        counter.fetch_add(100);
    });
    EXPECT_TRUE(res.valid());

    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_EQ(counter.load(), 101);
}

TEST(ThreadPoolTest, SubmitWithCallback) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::promise<void> p;
    auto future = p.get_future();

    pool.SubmitWithCallback(
            [&counter] { counter.fetch_add(1); },
            [&p] { p.set_value(); });

    future.wait_for(std::chrono::seconds(3));
    EXPECT_EQ(counter.load(), 1);
}

TEST(ThreadPoolTest, WorkStealing_LoadBalancing) {
    ThreadPool pool(4);
    std::vector<std::future<int>> results;

    // 只向线程 0 提交大量任务
    for (int i = 0; i < 10000; ++i) {
        results.push_back(pool.Submit([i] { return i * i; }));
    }

    int total = 0;
    for (auto &f: results) {
        total += f.get();
    }

    // 验证总和是否正确
    int expected = 0;
    for (int i = 0; i < 10000; ++i) {
        expected += i * i;
    }

    EXPECT_EQ(total, expected);
}

TEST(ThreadPoolTest, PauseAndResume) {
    ThreadPool pool(4);
    std::atomic<int> count{0};

    pool.Pause();
    for (int i = 0; i < 1000; ++i) {
        auto res = pool.Submit([&count] { count.fetch_add(1); });
        EXPECT_TRUE(res.valid());
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_EQ(count.load(), 0);// 应为 0，因为已暂停

    pool.Resume();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_GT(count.load(), 0);// 任务开始执行
}
