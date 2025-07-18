#pragma once
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

// 改进的CRTP基类 (C++17兼容)
template<class Derived>
struct ObjectBase {
    void cleanup() {
        static_assert(implements_cleanup<Derived>(0),
                      "Derived class must implement cleanup_impl() method");
        static_cast<Derived *>(this)->cleanup_impl();
    }

protected:
    ~ObjectBase() = default;

private:
    template<typename T>
    static constexpr auto implements_cleanup(int)
            -> decltype(std::declval<T>().cleanup_impl(), bool()) {
        return true;
    }

    template<typename T>
    static constexpr bool implements_cleanup(...) {
        return false;
    }
};

// 对象池实现 (C++17)
template<typename T>
class ObjectPool {
    static_assert(std::is_base_of<ObjectBase<T>, T>::value,
                  "T must inherit from ObjectBase<T>");

private:
    const size_t maxsize_;
    const size_t minsize_;
    std::atomic<size_t> count_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<T>> obj_list_;
    std::function<void(T *)> obj_destroy_func_;
    std::atomic_bool is_destructed_;

    std::shared_ptr<T> create_new_object() noexcept {
        try {
            auto obj = std::shared_ptr<T>(new T(), obj_destroy_func_);
            count_.fetch_add(1, std::memory_order_relaxed);
            return obj;
        } catch (...) {
            return nullptr;
        }
    }

    void initialize_pool() {
        std::vector<std::shared_ptr<T>> temp_objs;
        temp_objs.reserve(minsize_);

        for (size_t i = 0; i < minsize_; ++i) {
            if (auto obj = create_new_object()) {
                temp_objs.emplace_back(std::move(obj));
            } else {
                break;
            }
        }

        if (!temp_objs.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            obj_list_.insert(obj_list_.end(),
                             std::make_move_iterator(temp_objs.begin()),
                             std::make_move_iterator(temp_objs.end()));
        }
    }

public:
    ObjectPool(size_t minSize, size_t maxSize)
        : maxsize_(std::max(maxSize, minSize)),
          minsize_(minSize),
          count_(0),
          is_destructed_(false) {
        obj_list_.reserve(maxsize_);

        obj_destroy_func_ = [this](T *obj_ptr) {
            obj_ptr->cleanup();

            if (is_destructed_.load(std::memory_order_acquire)) {
                delete obj_ptr;
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                if (obj_list_.size() < maxsize_) {
                    try {
                        new (obj_ptr) T();// 原地构造
                        obj_list_.emplace_back(obj_ptr, obj_destroy_func_);
                    } catch (...) {
                        delete obj_ptr;
                        count_.fetch_sub(1, std::memory_order_relaxed);
                    }
                } else {
                    delete obj_ptr;
                    count_.fetch_sub(1, std::memory_order_relaxed);
                }
            }
        };

        initialize_pool();
    }

    ~ObjectPool() {
        is_destructed_.store(true, std::memory_order_release);
    }

    std::shared_ptr<T> RetrieveObject() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!obj_list_.empty()) {
            auto obj = std::move(obj_list_.back());
            obj_list_.pop_back();
            return obj;
        }

        if (count_.load(std::memory_order_relaxed) < maxsize_) {
            return create_new_object();
        }

        return nullptr;
    }

    std::vector<std::shared_ptr<T>> RetrieveObject(size_t n) noexcept {
        std::vector<std::shared_ptr<T>> result;
        if (n == 0) return result;

        result.reserve(n);
        std::lock_guard<std::mutex> lock(mutex_);

        size_t available = std::min(n, obj_list_.size());
        for (size_t i = 0; i < available; ++i) {
            result.emplace_back(std::move(obj_list_.back()));
            obj_list_.pop_back();
        }

        size_t remaining = n - available;
        size_t can_create = std::min(remaining,
                                     maxsize_ - count_.load(std::memory_order_relaxed));

        for (size_t i = 0; i < can_create; ++i) {
            if (auto obj = create_new_object()) {
                result.emplace_back(std::move(obj));
            } else {
                break;
            }
        }

        return result;
    }

    size_t GetPoolSize() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return obj_list_.size();
    }

    size_t GetTotalCount() const noexcept {
        return count_.load(std::memory_order_acquire);
    }

    void Preallocate(size_t n) {
        size_t to_allocate = std::min(n, maxsize_ - count_.load(std::memory_order_acquire));
        if (to_allocate == 0) return;

        std::vector<std::shared_ptr<T>> temp_objs;
        temp_objs.reserve(to_allocate);

        for (size_t i = 0; i < to_allocate; ++i) {
            if (auto obj = create_new_object()) {
                temp_objs.emplace_back(std::move(obj));
            } else {
                break;
            }
        }

        if (!temp_objs.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            obj_list_.insert(obj_list_.end(),
                             std::make_move_iterator(temp_objs.begin()),
                             std::make_move_iterator(temp_objs.end()));
        }
    }

    // 禁用拷贝和移动
    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;
    ObjectPool(ObjectPool &&) = delete;
    ObjectPool &operator=(ObjectPool &&) = delete;
};