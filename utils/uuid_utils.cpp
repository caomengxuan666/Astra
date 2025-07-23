#include "uuid_utils.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>


namespace Astra::utils {

    // 初始化线程局部变量
    thread_local std::mt19937_64 UuidGenerator::rng_;
    thread_local std::uniform_int_distribution<uint64_t> UuidGenerator::dist_(0, std::numeric_limits<uint64_t>::max());
    thread_local bool UuidGenerator::is_initialized_ = false;

    // 初始化随机数生成器（使用时间+线程ID作为种子）
    void UuidGenerator::initialize_rng() {
        if (!is_initialized_) {
            uint64_t seed = std::chrono::steady_clock::now().time_since_epoch().count();
            seed ^= static_cast<uint64_t>(std::hash<std::thread::id>()(std::this_thread::get_id()));
            rng_.seed(seed);
            is_initialized_ = true;
        }
    }

    // 生成UUID v4（格式：8-4-4-4-12）
    std::string UuidGenerator::generate() {
        initialize_rng();

        // 生成128位随机数（分两部分）
        uint64_t high = dist_(rng_);
        uint64_t low = dist_(rng_);

        // 设置UUID v4版本位（高64位的第13-16位）
        high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        // 设置变体位（高64位的第6-7位）
        high = (high & 0xFFFFFFF7FFFFFFFFULL) | 0x0000000800000000ULL;

        // 格式化输出
        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(16) << high << "-"
           << std::setw(4) << (low >> 48) << "-"
           << std::setw(4) << ((low >> 32) & 0xFFFF) << "-"
           << std::setw(4) << ((low >> 16) & 0xFFFF) << "-"
           << std::setw(12) << (low & 0xFFFFFFFFFFFFULL);

        return ss.str();
    }

    // 清理函数：重置生成器状态（对象池回收时调用）
    void UuidGenerator::cleanup_impl() {
        // 保留随机数生成器状态（避免重复初始化，提升性能）
        // 若需严格重置可添加：is_initialized_ = false;
    }

    // UuidUtils单例实现
    UuidUtils &UuidUtils::GetInstance() {
        static UuidUtils instance;
        return instance;
    }

    // 构造函数：初始化对象池（最小100个，最大10000000个生成器）
    constexpr size_t max_size = 100 * 10000;
    UuidUtils::UuidUtils() : pool_(100, max_size) {}

    // 从对象池获取UUID生成器
    std::shared_ptr<UuidGenerator> UuidUtils::get_generator() {
        return pool_.RetrieveObject();
    }

}// namespace Astra::utils