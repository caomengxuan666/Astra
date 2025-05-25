#pragma once

#include <core/noncopyable.hpp>
#include <functional>

namespace Astra {

    // ScopeGuard类实现RAII机制，确保在作用域退出时自动执行清理操作
    // 继承NonCopyable禁止拷贝构造和赋值操作
    class ScopeGuard : private NonCopyable {
    public:
        // 构造函数：接受一个无参数无返回值的函数对象作为作用域退出时的清理操作
        explicit ScopeGuard(std::function<void()> on_exit_scope)
            : on_exit_scope_(std::move(on_exit_scope)), dismissed_(false) {}

        // 析构函数：在对象销毁时（离开作用域）执行清理操作
        ~ScopeGuard() {
            if (!dismissed_) {
                on_exit_scope_();// 执行清理操作
            }
        }

        // 移动构造函数：允许将ScopeGuard对象移出当前作用域
        // 原对象不再负责执行清理操作
        ScopeGuard(ScopeGuard &&other) noexcept
            : on_exit_scope_(std::move(other.on_exit_scope_)), dismissed_(other.dismissed_) {
            other.dismissed_ = true;// 原对象不再拥有清理责任
        }

        // 禁用移动赋值操作符
        ScopeGuard &operator=(ScopeGuard &&) = delete;

        // Dismiss方法：手动取消清理操作
        // 调用后，析构函数不会再执行清理
        void Dismiss() {
            dismissed_ = true;
        }

        // is_dismissed方法：检查是否已取消清理操作
        // 返回true表示清理操作已被取消
        [[nodiscard]] bool is_dismissed() const {
            return dismissed_;
        }

    private:
        std::function<void()> on_exit_scope_;// 存储的清理操作函数对象
        bool dismissed_;                     // 标记是否已取消清理操作
    };

    // 工厂函数：创建ScopeGuard对象的便捷方式
    // 使用模板支持不同类型可调用对象的自动类型推导
    template<typename T>
    ScopeGuard MakeScopeGuard(T &&func) {
        return ScopeGuard(std::forward<T>(func));// 完美转发确保参数类型正确性
    }

}// namespace Astra