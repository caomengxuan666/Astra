#pragma once

#include <atomic>
#include <memory>

namespace Astra::datastructures {

    template<typename T, size_t Capacity = 1024>
    class LockFreeBufferBase {
    protected:
        // 构造函数保护，供派生类调用
        explicit LockFreeBufferBase(size_t capacity = Capacity)
            : buffer_(new T[capacity]), capacity_(capacity) {
            read_pos_.store(0, std::memory_order_relaxed);
            write_pos_.store(0, std::memory_order_relaxed);
        }

        // 获取下一个索引位置
        [[nodiscard]] size_t NextIndex(size_t current) const noexcept {
            return (current + 1) % capacity_;
        }

        // 检查是否为空
        [[nodiscard]] bool IsEmpty() const noexcept {
            return read_pos_.load(std::memory_order_acquire) == write_pos_.load(std::memory_order_acquire);
        }

        // 检查是否已满
        [[nodiscard]] bool IsFull(size_t wp) const noexcept {
            return NextIndex(wp) == read_pos_.load(std::memory_order_acquire);
        }

        // 获取当前写入指针位置（原子加载）
        [[nodiscard]] size_t LoadWriteIndex() const noexcept {
            return write_pos_.load(std::memory_order_relaxed);
        }

        // 获取当前读取指针位置（原子加载）
        [[nodiscard]] size_t LoadReadIndex() const noexcept {
            return read_pos_.load(std::memory_order_relaxed);
        }

        // 更新写入指针（原子存储）
        void StoreWriteIndex(size_t next_wp) noexcept {
            write_pos_.store(next_wp, std::memory_order_release);
        }

        // 更新读取指针（原子存储）
        void StoreReadIndex(size_t next_rp) noexcept {
            read_pos_.store(next_rp, std::memory_order_release);
        }

        // 尝试 CAS 更新读指针（用于 MPMC 场景）
        bool CASReadIndex(size_t expected, size_t desired) noexcept {
            return read_pos_.compare_exchange_weak(expected, desired,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed);
        }

        // 获取缓冲区大小
        [[nodiscard]] size_t GetCapacity() const noexcept {
            return capacity_;
        }

        // 获取缓冲区指针
        T *GetBuffer() const noexcept {
            return buffer_.get();
        }

        // 获取当前数据量
        [[nodiscard]] size_t Size() const noexcept {
            size_t wp = write_pos_.load();
            size_t rp = read_pos_.load();
            return wp >= rp ? wp - rp : wp + capacity_ - rp;
        }

    protected:
        const size_t capacity_;                // 缓冲区容量
        std::unique_ptr<T[]> buffer_;          // 数据存储
        mutable std::atomic<size_t> read_pos_; // 读指针
        mutable std::atomic<size_t> write_pos_;// 写指针
    };

}// namespace Astra::datastructures