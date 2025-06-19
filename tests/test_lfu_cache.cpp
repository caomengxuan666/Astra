#include "Astra-CacheServer/caching/AstraCacheStrategy.hpp"
#include "core/astra.hpp"
#include "logger.hpp"
#include <datastructures/lfu_cache.hpp>
#include <gtest/gtest.h>

using namespace Astra::datastructures;

TEST(LFUCacheTest, BasicPutAndGet) {
    AstraCache<LFUCache, int, int> cache(2);

    cache.Put(1, 10);
    cache.Put(2, 20);
    cache.Put(3, 30);

    EXPECT_FALSE(cache.Contains(1));
    EXPECT_TRUE(cache.Contains(2));
    EXPECT_TRUE(cache.Contains(3));
}

TEST(LFUCacheTest, UpdateExistingKey) {
    AstraCache<LFUCache, std::string, int> cache(2, 100,
                                                 std::chrono::seconds::zero(),
                                                 std::chrono::hours(24),// 很长的衰减时间
                                                 1.0);

    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("a", 10);// 更新 "a"，频率应增加

    auto a_value = cache.Get("a");
    ASSERT_TRUE(a_value.has_value()) << "Key 'a' should exist after update";
    EXPECT_EQ(a_value.value(), 10) << "Value of 'a' should be updated to 10";

    auto b_value = cache.Get("b");
    ASSERT_TRUE(b_value.has_value()) << "Key 'b' should still exist";
    EXPECT_EQ(b_value.value(), 2) << "Value of 'b' should remain 2";

    cache.Put("c", 3);// 应该淘汰 "b"

    b_value = cache.Get("b");
    ASSERT_FALSE(b_value.has_value()) << "Key 'b' should be evicted";

    auto c_value = cache.Get("c");
    ASSERT_TRUE(c_value.has_value()) << "Key 'c' should exist";
    EXPECT_EQ(c_value.value(), 3) << "Value of 'c' should be 3";
}

TEST(LFUCacheTest, EdgeCase_ZeroCapacity) {
    AstraCache<LFUCache, int, int> cache(0);

    cache.Put(1, 10);
    EXPECT_FALSE(cache.Contains(1));

    cache.Put(2, 20);
    EXPECT_FALSE(cache.Contains(2));
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(LFUCacheTest, StressTest_ManyInsertions) {
    AstraCache<LFUCache, int, int> cache(100);

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

TEST(LFUCacheTest, HotKeyNotEvicted) {
    LFUCache<int, int> cache(3, 100,
                             std::chrono::seconds::zero(),
                             std::chrono::hours(24),
                             1.0);// 确保线性增长

    // 初始插入
    cache.Put(1, 10);
    cache.Put(2, 20);
    cache.Put(3, 30);

    // 确保初始状态
    ASSERT_TRUE(cache.Contains(1));
    ASSERT_TRUE(cache.Contains(2));
    ASSERT_TRUE(cache.Contains(3));

    // 制造热点键 - 只访问键1
    for (int i = 0; i < 200; ++i) {
        cache.Get(1);
    }

    // 确保键2和3没有被意外访问
    // 插入新键触发淘汰
    cache.Put(4, 40);

    // 验证结果 - 应该保留 key=1、key=4，淘汰 key=2 或 key=3 中的一个
    EXPECT_TRUE(cache.Contains(1)) << "Hot key 1 should remain";
    EXPECT_TRUE(cache.Contains(4)) << "New key 4 should exist";

    // 确保缓存大小仍是 3
    EXPECT_EQ(cache.Size(), 3) << "Cache size should still be 3";

    // 确保淘汰了一个 key（2 或 3 中的一个）
    EXPECT_TRUE(!cache.Contains(2) || !cache.Contains(3))
            << "At least one of key 2 or 3 should be evicted";

    // 确保没有同时保留两个
    EXPECT_FALSE(cache.Contains(2) && cache.Contains(3))
            << "Both key 2 and 3 should not exist at the same time";

    auto res = cache.GetAllEntries();
    //调试res
    for (auto &entry: res) {
        ZEN_LOG_INFO("key: {}, value: {}", entry.first, entry.second);
    }
}

TEST(LFUCacheTest, TTLExpiration) {
    AstraCache<LFUCache, int, int> cache(2, 100, std::chrono::seconds(1));// 容量2，TTL 1秒

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

TEST(LFUCacheTest, MixedTTLAndLFU) {
    AstraCache<LFUCache, int, int> cache(2, 100, std::chrono::seconds(5));// 容量2，TTL 5秒

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

TEST(LFUCacheTest, ZeroTTL) {
    AstraCache<LFUCache, int, int> cache(2, 100, std::chrono::seconds(0));// 零TTL

    cache.Put(1, 10);

    // 立即检查
    EXPECT_EQ(cache.Get(1).value(), 10);

    // 等待一点时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 零TTL项应该永不过期
    EXPECT_TRUE(cache.Get(1).has_value());
}

TEST(LFUCacheTest, TTLUpdateOnGet) {
    AstraCache<LFUCache, int, int> cache(2, 100, std::chrono::seconds(1));// 容量2，TTL 1秒

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