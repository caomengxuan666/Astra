#pragma once
#include "object_pool.hpp"
#include <cstdint>
#include <random>
#include <string>


namespace Astra::utils {

    // UUID生成器（基于对象池的可复用对象）
    class UuidGenerator : public ObjectBase<UuidGenerator> {
    public:
        // 生成UUID v4（随机生成）
        std::string generate();

        // 对象池回收时的清理函数（重置随机数状态）
        void cleanup_impl();

    private:
        // 线程局部随机数生成器（避免锁竞争）
        thread_local static std::mt19937_64 rng_;
        thread_local static std::uniform_int_distribution<uint64_t> dist_;
        thread_local static bool is_initialized_;

        // 初始化随机数生成器（线程安全）
        static void initialize_rng();
    };

    // UUID工具类（封装对象池）
    class UuidUtils {
    public:
        // 单例模式（全局唯一对象池）
        static UuidUtils &GetInstance();

        // 获取UUID生成器（从对象池）
        std::shared_ptr<UuidGenerator> get_generator();

        // 禁用拷贝和移动
        UuidUtils(const UuidUtils &) = delete;
        UuidUtils &operator=(const UuidUtils &) = delete;
        UuidUtils(UuidUtils &&) = delete;
        UuidUtils &operator=(UuidUtils &&) = delete;

    private:
        UuidUtils();
        ~UuidUtils() = default;

        // 对象池实例（最小100个，最大10000000个生成器）
        ObjectPool<UuidGenerator> pool_;
    };

}// namespace Astra::utils