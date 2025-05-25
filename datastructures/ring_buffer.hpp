#pragma once

#include "lock_free_buffer_base.hpp"

namespace Astra::datastructures {

    template<typename T, size_t Capacity = 1024>
    class RingBuffer : public LockFreeBufferBase<T, Capacity> {
    public:
        using LockFreeBufferBase<T, Capacity>::LockFreeBufferBase;

        // 单生产者写入（SPSC）
        bool Push(const T &item) {
            const size_t wp = this->LoadWriteIndex();
            if (this->IsFull(wp)) {
                return false;// 队列已满
            }

            this->buffer_[wp] = item;
            this->StoreWriteIndex(this->NextIndex(wp));
            return true;
        }

        // 单消费者读取（SPSC）
        bool Pop(T &item) {
            const size_t rp = this->LoadReadIndex();
            if (rp == this->write_pos_.load(std::memory_order_acquire)) {
                return false;// 队列空
            }

            item = this->buffer_[rp];
            this->StoreReadIndex(this->NextIndex(rp));
            return true;
        }

        using LockFreeBufferBase<T, Capacity>::IsEmpty;
        using LockFreeBufferBase<T, Capacity>::IsFull;
        using LockFreeBufferBase<T, Capacity>::Size;

        [[nodiscard]] bool Empty() const noexcept {
            return LockFreeBufferBase<T, Capacity>::IsEmpty();
        }

        [[nodiscard]] bool Full() const noexcept {
            return LockFreeBufferBase<T, Capacity>::IsFull(
                    LockFreeBufferBase<T, Capacity>::LoadWriteIndex());
        }

        [[nodiscard]] size_t Size() const noexcept {
            return LockFreeBufferBase<T, Capacity>::Size();
        }
    };

}// namespace Astra::datastructures