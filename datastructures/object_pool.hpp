#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

template<typename T, typename = void>
struct has_cleanup : std::false_type {};

template<typename T>
struct has_cleanup<T, std::void_t<decltype(std::declval<T>().cleanup())>> : std::true_type {};

// 对象池
// 可以结合单例模式使用
template<typename T>
class ObjectPool {
private:
    int maxsize_;
    int minsize_;
    int count_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<T>> obj_list_;
    std::function<void(T *obj_ptr)> obj_destroy_func_;
    std::atomic_bool is_destructed_;

public:
    ObjectPool(int minSize, int maxSize) {
        static_assert(has_cleanup<T>::value, "T must have a cleanup() function");
        maxsize_ = maxSize;
        minsize_ = minSize;
        obj_list_.reserve(maxSize);

        obj_destroy_func_ = [&](T *obj_ptr) {
            if (is_destructed_ == true) {
                obj_ptr->cleanup();
                delete obj_ptr;
            } else {
                // 加入回收池
                obj_ptr->cleanup();
                // 在对象被销毁前重置其状态
                obj_ptr->value = 0;
                std::lock_guard<std::mutex> lock(mutex_);
                obj_list_.push_back(std::shared_ptr<T>(obj_ptr, obj_destroy_func_));
            }
        };
        for (int i = 0; i < minsize_; i++) {
            obj_list_.emplace_back(new T(), obj_destroy_func_);
        }
        count_ = minsize_;
    }
    ~ObjectPool() {
        is_destructed_ = true;
    }

    std::shared_ptr<T> GetObject() {
        std::shared_ptr<T> result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (obj_list_.empty()) {
            if (count_ < maxsize_) {
                count_++;
                result = std::shared_ptr<T>(new T(), obj_destroy_func_);
            } else {
                // throw std::runtime_error("ObjectPool is full");
                return nullptr;
            }
        } else {
            result = obj_list_.back();
            obj_list_.pop_back();
        }
        return result;
    }
};