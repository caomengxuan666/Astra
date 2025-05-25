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

    std::cout << "Hit count: " << hitCount << "/" << WriteIterations << std::endl;
}
