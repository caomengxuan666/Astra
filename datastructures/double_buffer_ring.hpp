#pragma once

#include "ring_buffer.hpp"
#include <atomic>

namespace Astra::datastructures {

    template<typename T, size_t Capacity = 1024>
    class LockFreeDoubleBuffer {
    public:
        using BufferType = RingBuffer<T, Capacity>;

        LockFreeDoubleBuffer()
            : active_read_index_(0), active_write_index_(1) {}

        // 生产者写入接口（线程安全）
        bool Push(const T &item) {
            size_t wp = active_write_index_.load(std::memory_order_acquire);
            if (buffers_[wp].Push(item)) {
                return true;
            }

            // 如果当前写入缓冲区已满，尝试切换缓冲区
            size_t new_wp = 1 - wp;
            if (buffers_[new_wp].Empty()) {
                // 等待消费者完成当前缓冲区的读取
                while (!buffers_[wp].Empty()) {
                    std::this_thread::yield();
                }
                active_write_index_.store(new_wp, std::memory_order_release);
                return buffers_[new_wp].Push(item);
            }
            return false;
        }


        // 消费者读取接口（线程安全）
        void SwapBuffers() {
            size_t current_write = active_write_index_.load(std::memory_order_relaxed);
            size_t current_read = active_read_index_.load(std::memory_order_relaxed);

            if (!buffers_[current_write].Empty()) {
                // 等待消费者完成当前写入缓冲区的读取
                while (buffers_[current_write].Size() > 0) {
                    std::this_thread::yield();
                }
                // 切换读写缓冲区
                active_read_index_.store(current_write, std::memory_order_release);
                active_write_index_.store(1 - current_write, std::memory_order_release);
            }
        }

        // 消费者从当前读缓冲区 Pop 数据
        bool Pop(T &item) {
            size_t rp = active_read_index_.load(std::memory_order_acquire);
            if (buffers_[rp].Pop(item)) {
                return true;
            }

            // 如果当前读缓冲区为空，尝试切换到另一个缓冲区
            size_t new_rp = 1 - rp;
            if (!buffers_[new_rp].Empty()) {
                active_read_index_.store(new_rp, std::memory_order_release);
                return buffers_[new_rp].Pop(item);
            }
            return false;
        }

        // 获取当前读缓冲区的数据量
        [[nodiscard]] size_t Size() const noexcept {
            size_t rp = active_read_index_.load();
            return buffers_[rp].Size();
        }

        // 缓冲区是否为空
        [[nodiscard]] bool Empty() const noexcept {
            size_t rp = active_read_index_.load();
            return buffers_[rp].Empty();
        }

    private:
        alignas(64) std::atomic<size_t> active_read_index_;
        alignas(64) std::atomic<size_t> active_write_index_;
        BufferType buffers_[2];// 两个缓冲区交替使用
    };

}// namespace Astra::datastructures