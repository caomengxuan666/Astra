#include "core/astra.hpp"
#include <atomic>
#include <datastructures/thread_safe_lru_cache.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace Astra::datastructures;

TEST(ThreadSafeLRUCacheTest, ConcurrentPutAndGet) {
    ThreadSafeLRUCache<int, int> cache(1000);

    constexpr int NumThreads = 4;
    constexpr int ItemsPerThread = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < NumThreads; ++t) {
        threads.emplace_back([&cache, t](int tid) {
            for (int i = 0; i < ItemsPerThread; ++i) {
                cache.Put(tid * ItemsPerThread + i, tid);
            }
        },
                             t);
    }

    for (auto &t: threads) {
        if (t.joinable()) t.join();
    }

    // 验证所有数据是否正确（允许部分被淘汰）
    for (int t = 0; t < NumThreads; ++t) {
        int hits = 0;
        for (int i = 0; i < ItemsPerThread; ++i) {
            int key = t * ItemsPerThread + i;
            auto val = cache.Get(key);
            if (val.has_value()) {
                hits++;
                EXPECT_EQ(val.value(), t);
            }
        }
        std::cout << "Thread " << t << " hit rate: " << hits << "/" << ItemsPerThread << std::endl;
    }
}

TEST(ThreadSafeLRUCacheTest, StressTest_ManyThreads) {
    ThreadSafeLRUCache<int, int> cache(1000);

    std::atomic<bool> done(false);
    std::vector<std::thread> writers;
    std::vector<std::thread> readers;

    // 写入线程
    for (int i = 0; i < 4; ++i) {
        writers.emplace_back([&cache, &done](int base) {
            while (!done.load()) {
                for (int j = 0; j < 1000; ++j) {
                    cache.Put(base + j, base + j);
                }
            }
        },
                             i * 1000);
    }

    // 读取线程
    std::atomic<int> read_count(0);
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&cache, &read_count, &done]() {
            while (!done.load() || read_count.load() < 40000) {
                for (int j = 0; j < 10000; ++j) {
                    auto val = cache.Get(j);
                    if (val.has_value()) {
                        ++read_count;
                    }
                }
            }
        });
    }

    done.store(true);

    for (auto &t: writers) {
        if (t.joinable()) t.join();
    }

    for (auto &t: readers) {
        if (t.joinable()) t.join();
    }

    EXPECT_GT(read_count.load(), 0);
}