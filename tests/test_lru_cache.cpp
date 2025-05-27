#include "core/astra.hpp"
#include <datastructures/lru_cache.hpp>
#include <gtest/gtest.h>

using namespace Astra::datastructures;

TEST(LRUCacheTest, BasicPutAndGet) {
    LRUCache<int, int> cache(2);

    cache.Put(1, 10);
    cache.Put(2, 20);

    EXPECT_TRUE(cache.Contains(1));
    EXPECT_TRUE(cache.Contains(2));

    EXPECT_EQ(cache.Get(1).value(), 10);
    EXPECT_EQ(cache.Get(2).value(), 20);

    cache.Put(3, 30);// 应该淘汰 key=1

    EXPECT_FALSE(cache.Contains(1));
    EXPECT_EQ(cache.Get(2).value(), 20);
    EXPECT_EQ(cache.Get(3).value(), 30);
}

TEST(LRUCacheTest, UpdateExistingKey) {
    LRUCache<std::string, int> cache(2);

    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("a", 10);// 更新 "a" 并置顶

    auto a_value = cache.Get("a");
    ASSERT_TRUE(a_value.has_value()) << "Key 'a' should exist after update";
    EXPECT_EQ(a_value.value(), 10) << "Value of 'a' should be updated to 10";

    auto b_value = cache.Get("b");
    ASSERT_TRUE(b_value.has_value()) << "Key 'b' should still exist";
    EXPECT_EQ(b_value.value(), 2) << "Value of 'b' should remain 2";

    cache.Put("c", 3);// 应该淘汰 "a"

    a_value = cache.Get("a");
    ASSERT_FALSE(a_value.has_value()) << "Key 'a' should be evicted";// 修改此处，预期a被淘汰

    EXPECT_TRUE(cache.Contains("b")) << "Key 'b' should still exist";// 检查b存在

    auto c_value = cache.Get("c");
    ASSERT_TRUE(c_value.has_value()) << "Key 'c' should exist";
    EXPECT_EQ(c_value.value(), 3) << "Value of 'c' should be 3";
}
TEST(LRUCacheTest, EdgeCase_ZeroCapacity) {
    LRUCache<int, int> cache(0);

    cache.Put(1, 10);
    EXPECT_FALSE(cache.Contains(1));

    cache.Put(2, 20);
    EXPECT_FALSE(cache.Contains(2));
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(LRUCacheTest, StressTest_ManyInsertions) {
    LRUCache<int, int> cache(100);

    for (int i = 0; i < 150; ++i) {
        cache.Put(i, i * 2);
    }

    for (int i = 0; i < 150; ++i) {
        auto result = cache.Get(i);
        if (i < 50) {
            EXPECT_FALSE(result.has_value());// 被淘汰
        } else {
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result.value(), i * 2);
        }
    }
}

TEST(LRUCacheTest, StressTest_WithManyOperations) {
    LRUCache<int, int> cache(100);

    // 插入大量的键值对
    constexpr int WriteIterations = 10000;
    for (int i = 0; i < WriteIterations; ++i) {
        cache.Put(i, i);
    }

    // 验证最近插入的数据是否存在
    int hitCount = 0;
    for (int i = 0; i < WriteIterations; ++i) {
        auto val = cache.Get(i);
        if (val.has_value()) {
            ++hitCount;
            EXPECT_EQ(val.value(), i);
        }
    }

    ZEN_LOG_INFO("Hit count: {} / {}", hitCount, WriteIterations);
}

TEST(LRUCacheTest, RemoveExistingKey) {
    LRUCache<int, std::string> cache(3);

    cache.Put(1, "one");
    EXPECT_TRUE(cache.Contains(1));

    bool removed = cache.Remove(1);
    EXPECT_TRUE(removed);
    EXPECT_FALSE(cache.Contains(1));
}

TEST(LRUCacheTest, RemoveNonExistentKey) {
    LRUCache<int, std::string> cache(3);
    bool removed = cache.Remove(999);
    EXPECT_FALSE(removed);// 不存在的键返回 false
}

// 测试TTL功能
TEST(LRUCacheTest, TTLExpiration) {
    LRUCache<int, int> cache(2, 100, std::chrono::seconds(1));// 容量2，热点阈值100，TTL 1秒

    cache.Put(1, 10);
    cache.Put(2, 20);

    // 立即检查
    EXPECT_EQ(cache.Get(1).value(), 10);
    EXPECT_EQ(cache.Get(2).value(), 20);

    // 等待超过TTL
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 检查是否过期
    EXPECT_FALSE(cache.Get(1).has_value());
    EXPECT_FALSE(cache.Contains(1));
    EXPECT_FALSE(cache.Get(2).has_value());
    EXPECT_FALSE(cache.Contains(2));
}

// 测试混合TTL和LRU淘汰
TEST(LRUCacheTest, MixedTTLAndLRU) {
    LRUCache<int, int> cache(2, 100, std::chrono::seconds(5));// 容量2，TTL 5秒

    cache.Put(1, 10);                         // 使用默认TTL
    cache.Put(2, 20, std::chrono::seconds(1));// 自定义TTL 1秒

    // 立即检查
    EXPECT_EQ(cache.Get(1).value(), 10);
    EXPECT_EQ(cache.Get(2).value(), 20);

    // 等待1.5秒
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // 检查定制TTL项是否过期
    EXPECT_FALSE(cache.Get(2).has_value());

    // 默认TTL项应该仍然存在
    EXPECT_EQ(cache.Get(1).value(), 10);
}

// 测试定期清理任务
TEST(LRUCacheTest, PeriodicCleanup) {
    Astra::concurrent::TaskQueue task_queue;
    LRUCache<int, int> cache(2, 100, std::chrono::seconds(1));// 容量2，TTL 1秒

    // 启动清理任务（每1秒执行一次）
    cache.StartEvictionTask(task_queue, std::chrono::seconds(1));

    // 插入缓存项
    cache.Put(1, 10);

    // 等待足够时间让清理任务执行多次
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 验证清理任务是否持续运行
    // （如果清理任务正常工作，缓存项应该已经过期）
    EXPECT_FALSE(cache.Get(1).has_value());
}

// 测试零TTL
TEST(LRUCacheTest, ZeroTTL) {
    LRUCache<int, int> cache(2, 100, std::chrono::seconds(0));// 零TTL

    cache.Put(1, 10);

    // 立即检查
    EXPECT_EQ(cache.Get(1).value(), 10);

    // 等待一点时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 零TTL项应该永不过期
    EXPECT_TRUE(cache.Get(1).has_value());
}

// 测试TTL更新
TEST(LRUCacheTest, TTLUpdateOnGet) {
    LRUCache<int, int> cache(2, 100, std::chrono::seconds(1));// 容量2，TTL 1秒

    cache.Put(1, 10);

    // 立即检查
    EXPECT_EQ(cache.Get(1).value(), 10);

    // 等待500毫秒
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 再次获取应该重置TTL
    EXPECT_EQ(cache.Get(1).value(), 10);

    // 再等待800毫秒（总共1.3秒）
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // 应该过期
    EXPECT_FALSE(cache.Get(1).has_value());
}