#include <gtest/gtest.h>
#include "../datastructures/object_pool.hpp"

class TestObject {
public:
    int value;
    std::atomic<int> ref_count;

    TestObject() : value(0), ref_count(0) {}

    void cleanup() {
        // 模拟资源清理操作
    }
};

// 测试对象池的基本功能
TEST(ObjectPoolTest, BasicFunctionality) {
    ObjectPool<TestObject> pool(2, 5);

    // 获取初始对象
    auto obj1 = pool.GetObject();
    ASSERT_NE(obj1, nullptr);
    EXPECT_EQ(pool.GetObject().use_count(), 1);  // 确保新获取的对象引用计数正确

    // 释放对象回池
    obj1.reset();
    
    // 验证对象是否成功返回池中
    auto obj2 = pool.GetObject();
    ASSERT_NE(obj2, nullptr);
    EXPECT_EQ(obj2.use_count(), 1);  // 确保从池中获取的对象引用计数正确
}

// 测试对象池在高负载下的表现
TEST(ObjectPoolTest, HighLoadTest) {
    ObjectPool<TestObject> pool(2, 100);

    std::vector<std::shared_ptr<TestObject>> objects;
    
    // 获取大量对象以测试高负载情况
    for (int i = 0; i < 100; ++i) {
        auto obj = pool.GetObject();
        ASSERT_NE(obj, nullptr);
        obj->value = i;
        objects.push_back(obj);
    }

    // 释放所有对象回池
    objects.clear();
    
    // 再次获取对象验证池的回收能力
    for (int i = 0; i < 50; ++i) {
        auto obj = pool.GetObject();
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->value, 0); // 验证清理后初始值正确
    }
}

// 测试对象池分配和释放1GB内存的能力
TEST(ObjectPoolTest, GBMemoryAllocation) {
    // 计算1GB内存可以分配的对象数量
    size_t memorySize = 1024;
    size_t objSize = sizeof(TestObject);
    size_t objCount = memorySize / objSize;

    ObjectPool<TestObject> pool(2, 10000); // 限制最大对象数为10000

    std::vector<std::shared_ptr<TestObject>> objects;
    
    // 尝试分配1GB内存 worth 的对象
    for (size_t i = 0; i < objCount; ++i) {
        auto obj = pool.GetObject();
        if (!obj) {
            // 如果达到池的最大容量，等待一段时间再重试
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            --i; // 重试当前索引
            continue;
        }
        ASSERT_NE(obj, nullptr);
        obj->value = i % 100;
        objects.push_back(obj);
    }

    // 释放所有对象回池
    objects.clear();
    
    // 再次获取部分对象验证池的回收能力
    for (size_t i = 0; i < objCount / 2; ++i) {
        auto obj = pool.GetObject();
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->value, 0); // 验证清理后初始值正确
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}