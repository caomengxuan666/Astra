#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <utility>// 用于std::forward

namespace Astra::datastructures {

    enum class OverflowPolicy {
        DROP,
        BLOCK,
        RESIZE
    };

    template<typename T, size_t Capacity = 1024, OverflowPolicy Policy = OverflowPolicy::DROP>
    class LockFreeQueue {
    public:
        // STL兼容的类型定义
        using value_type = T;
        using reference = T &;
        using const_reference = const T &;
        using pointer = T *;
        using const_pointer = const T *;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;

        explicit LockFreeQueue(size_t capacity = Capacity)
            : buffer_(new T[capacity + 1]) {
            capacity_ = capacity + 1;// 环形队列预留一个空位
            read_pos_.store(0, std::memory_order_relaxed);
            write_pos_.store(0, std::memory_order_relaxed);
        }

        // 禁止拷贝构造和赋值（无锁结构拷贝复杂，如需请自行实现）
        LockFreeQueue(const LockFreeQueue &) = delete;
        LockFreeQueue &operator=(const LockFreeQueue &) = delete;

        // 移动构造和赋值
        LockFreeQueue(LockFreeQueue &&other) noexcept
            : buffer_(std::move(other.buffer_)),
              capacity_(other.capacity_),
              read_pos_(other.read_pos_.load()),
              write_pos_(other.write_pos_.load()) {}

        LockFreeQueue &operator=(LockFreeQueue &&other) noexcept {
            buffer_ = std::move(other.buffer_);
            capacity_ = other.capacity_;
            read_pos_.store(other.read_pos_.load());
            write_pos_.store(other.write_pos_.load());
            return *this;
        }

        // STL风格：入队（支持拷贝）
        bool push(const T &item) {
            return try_push(item);
        }

        // STL风格：入队（支持移动）
        bool push(T &&item) {
            return try_push(std::move(item));
        }

        // STL风格：原地构造元素入队
        template<typename... Args>
        bool emplace(Args &&...args) {
            size_t wp = write_pos_.load(std::memory_order_relaxed);
            size_t next_wp = (wp + 1) % capacity_;

            // 检查队列是否已满
            if (next_wp == read_pos_.load(std::memory_order_acquire)) {
                if constexpr (Policy == OverflowPolicy::DROP) {
                    return false;
                } else if constexpr (Policy == OverflowPolicy::BLOCK) {
                    // 阻塞等待直到有空间（循环检查，避免虚假唤醒）
                    while (next_wp == read_pos_.load(std::memory_order_acquire)) {
                        std::this_thread::yield();
                    }
                } else if constexpr (Policy == OverflowPolicy::RESIZE) {
                    // 扩容（简单实现，实际使用需考虑线程安全）
                    if (next_wp == read_pos_.load(std::memory_order_acquire)) {
                        resize(capacity_ * 2);
                        wp = write_pos_.load(std::memory_order_relaxed);
                        next_wp = (wp + 1) % capacity_;
                    }
                }
            }

            // 原地构造元素
            new (&buffer_[wp]) T(std::forward<Args>(args)...);
            write_pos_.store(next_wp, std::memory_order_release);
            return true;
        }

        // STL风格：出队（将元素写入item）
        bool pop(T &item) {
            while (true) {
                // 1. 加载当前读指针（acquire确保可见性）
                size_t rp = read_pos_.load(std::memory_order_acquire);
                // 2. 加载当前写指针（acquire确保可见性）
                size_t wp = write_pos_.load(std::memory_order_acquire);

                // 检查队列是否为空
                if (rp == wp) {
                    return false;// 队列为空，退出
                }

                // 3. 预计算下一个读指针
                size_t next_rp = (rp + 1) % capacity_;

                // 4. 尝试原子更新读指针（CAS操作）
                // 若当前read_pos_仍为rp，则更新为next_rp，返回true
                if (read_pos_.compare_exchange_strong(
                            rp,                       // 预期值：当前rp
                            next_rp,                  // 目标值：next_rp
                            std::memory_order_release,// 成功时的内存序
                            std::memory_order_acquire // 失败时的内存序
                            )) {
                    // 5. CAS成功，安全读取元素
                    item = std::move(buffer_[rp]);
                    // 销毁原元素（非POD类型需要）
                    buffer_[rp].~T();
                    return true;
                }

                // CAS失败：说明其他线程已更新read_pos_，重试循环
            }
        }

        // STL风格：检查是否为空
        [[nodiscard]] bool empty() const noexcept {
            return read_pos_.load(std::memory_order_acquire) == write_pos_.load(std::memory_order_acquire);
        }

        // STL风格：获取当前元素数量
        [[nodiscard]] size_type size() const noexcept {
            size_t rp = read_pos_.load(std::memory_order_acquire);
            size_t wp = write_pos_.load(std::memory_order_acquire);
            return (wp >= rp) ? (wp - rp) : (wp + capacity_ - rp);
        }

        // STL风格：获取容量
        [[nodiscard]] size_type capacity() const noexcept {
            return capacity_ - 1;// 减去预留的空位
        }

        // 兼容旧接口：入队（供原有代码使用）
        bool Push(const T &item) {
            return push(item);
        }

        // 兼容旧接口：出队（供原有代码使用）
        bool Pop(T &item) {
            return pop(item);
        }

        // 兼容旧接口：检查是否为空
        [[nodiscard]] bool Empty() const {
            return empty();
        }

        // 兼容旧接口：获取元素数量
        [[nodiscard]] size_t Size() const {
            return size();
        }

    private:
        // 内部通用入队实现（支持拷贝和移动）
        template<typename U>
        bool try_push(U &&item) {
            size_t wp = write_pos_.load(std::memory_order_relaxed);
            size_t next_wp = (wp + 1) % capacity_;

            // 检查队列是否已满
            if (next_wp == read_pos_.load(std::memory_order_acquire)) {
                if constexpr (Policy == OverflowPolicy::DROP) {
                    return false;
                } else if constexpr (Policy == OverflowPolicy::BLOCK) {
                    // 阻塞等待直到有空间
                    while (next_wp == read_pos_.load(std::memory_order_acquire)) {
                        std::this_thread::yield();
                    }
                } else if constexpr (Policy == OverflowPolicy::RESIZE) {
                    if (next_wp == read_pos_.load(std::memory_order_acquire)) {
                        resize(capacity_ * 2);
                        wp = write_pos_.load(std::memory_order_relaxed);
                        next_wp = (wp + 1) % capacity_;
                    }
                }
            }

            // 写入元素（拷贝或移动）
            buffer_[wp] = std::forward<U>(item);
            write_pos_.store(next_wp, std::memory_order_release);
            return true;
        }

        // 扩容（简单实现，多线程环境需谨慎使用）
        void resize(size_t new_capacity) {
            if (new_capacity <= capacity_) return;

            // 分配新缓冲区
            std::unique_ptr<T[]> new_buffer(new T[new_capacity]);
            size_t new_wp = 0;

            // 迁移旧数据
            T item;
            while (Pop(item)) {
                new_buffer[new_wp++] = std::move(item);
            }

            // 更新缓冲区和指针
            capacity_ = new_capacity;
            buffer_ = std::move(new_buffer);
            read_pos_.store(0, std::memory_order_relaxed);
            write_pos_.store(new_wp, std::memory_order_relaxed);
        }

        std::unique_ptr<T[]> buffer_;  // 存储元素的缓冲区
        size_t capacity_;              // 缓冲区大小（含预留空位）
        std::atomic<size_t> read_pos_; // 读指针（下一个要读取的位置）
        std::atomic<size_t> write_pos_;// 写指针（下一个要写入的位置）
    };

}// namespace Astra::datastructures