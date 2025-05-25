#pragma once

#include <atomic>
#include <memory>
#include <thread>

namespace Astra::datastructures {

    enum class OverflowPolicy {
        DROP,
        BLOCK,
        RESIZE
    };

    template<typename T, size_t Capacity = 1024, OverflowPolicy Policy = OverflowPolicy::DROP>
    class LockFreeQueue {
    public:
        explicit LockFreeQueue(size_t capacity = Capacity)
            : buffer_(new T[capacity + 1]) {
            capacity_ = capacity + 1;
            read_pos_.store(0, std::memory_order_relaxed);
            write_pos_.store(0, std::memory_order_relaxed);
        }

        bool Push(const T &item) {
            size_t wp = write_pos_.load(std::memory_order_relaxed);
            size_t next_wp = (wp + 1) % capacity_;

            if (next_wp == read_pos_.load(std::memory_order_acquire)) {
                if constexpr (Policy == OverflowPolicy::DROP) {
                    return false;
                } else if constexpr (Policy == OverflowPolicy::BLOCK) {
                    while (next_wp == read_pos_.load(std::memory_order_acquire)) {
                        std::this_thread::yield();
                    }
                } else if constexpr (Policy == OverflowPolicy::RESIZE) {
                    // TODO: 支持扩容
                    if (next_wp == read_pos_.load(std::memory_order_acquire)) {
                        Resize(capacity_ * 2);// 扩容为原来的两倍
                    }
                }
            }

            buffer_[wp] = item;
            write_pos_.store(next_wp, std::memory_order_release);
            return true;
        }

        bool Pop(T &item) {
            size_t expected_rp = read_pos_.load(std::memory_order_relaxed);

            while (true) {
                size_t current_wp = write_pos_.load(std::memory_order_acquire);
                if (expected_rp == current_wp) {
                    return false;// 队列空
                }

                size_t next_rp = (expected_rp + 1) % capacity_;

                // 尝试原子化地更新 read_pos_
                if (read_pos_.compare_exchange_weak(expected_rp, next_rp,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed)) {
                    // 成功抢到这个位置，可以安全读取
                    item = buffer_[expected_rp];
                    return true;
                }

                // 如果 compare_exchange_weak 失败，则 expected_rp 已被更新，继续循环
            }
        }

        [[nodiscard]] bool Empty() const {
            return read_pos_.load() == write_pos_.load();
        }

        [[nodiscard]] size_t Size() const {
            size_t wp = write_pos_.load();
            size_t rp = read_pos_.load();
            return wp >= rp ? wp - rp : wp + capacity_ - rp;
        }

    private:
        void Resize(size_t new_capacity) {
            T *new_buffer = new T[new_capacity];
            size_t rp = read_pos_.load();
            size_t wp = write_pos_.load();
            size_t count = (wp >= rp ? wp - rp : capacity_ - rp + wp);

            for (size_t i = 0; i < count; ++i) {
                new_buffer[i] = std::move(buffer_[(rp + i) % capacity_]);
            }

            read_pos_.store(0, std::memory_order_relaxed);
            write_pos_.store(count, std::memory_order_relaxed);
            delete[] buffer_.release();
            buffer_.reset(new_buffer);
            capacity_ = new_capacity;
        }
        size_t capacity_;
        std::unique_ptr<T[]> buffer_;
        std::atomic<size_t> read_pos_;
        std::atomic<size_t> write_pos_;
    };

}// namespace Astra::datastructures