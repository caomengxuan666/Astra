#pragma once
#include "noncopyable.hpp"
#include <chrono>
#include <optional>
#include <vector>
namespace Astra::datastructures {
    //使用CRTP，我们的缓存见不得虚函数表开销的，所以尽量做零成本抽象
    /**
     * @author       : caomengxuan
     * @brief        : LRUStratgyAbstract，只用于规范LRU数据结构本身的公共操作接口，不涉及操作细节和成员变量的设计
                       通过CRTP的方式来调用具体的子类接口，从而实现缓存策略的抽象
     * @note         : 此类不允许被复制,
     * @return        {*}
    **/
    template<typename Derived, typename Key, typename Value>
    class AstraCacheStratgy : public NonCopyable {
    public:
        //必备的Get，获取缓存接口
        std::optional<Value> Get(const Key &key) {
            return static_cast<Derived *>(this)->Get(key);
        }
        //必备Put,插入和更新缓存接口
        void Put(const Key &key, const Value &value, std::chrono::seconds ttl = std::chrono::seconds::zero()) {
            static_cast<Derived *>(this)->Put(key, value, ttl);
        }

        //获取所有键的接口
        std::vector<Key> GetKeys() const {
            return static_cast<const Derived *>(this)->GetKeys();
        }

        //获取所有值
        std::vector<Value> GetValues() const {
            return static_cast<const Derived *>(this)->GetValues();
        }

        //清空缓存
        void Clear() {
            static_cast<Derived *>(this)->Clear();
        }

        //删除指定键
        bool Remove(const Key &key) {
            return static_cast<Derived *>(this)->Remove(key);
        }

        //必备获取key的接口
        [[nodiscard]] bool Contains(const Key &key) const {
            return static_cast<const Derived *>(this)->Contains(key);
        }


        //获取缓存大小的接口
        [[nodiscard]] size_t Size() const {
            return static_cast<const Derived *>(this)->Size();
        }

        //获取容量的接口
        [[nodiscard]] size_t Capacity() const {
            return static_cast<const Derived *>(this)->Capacity();
        }
    };

    /**
     * @author       : caomengxuan
     * @brief        : 这个类调用具体的缓存策略类，每个具体的缓存类都提供一个接口，直接在这里调用。
                       我们缓存策略类本身只需要继承AstraCacheStratgyAbstract并且实现每个接口。
     * @return        {*}
    **/
    template<template<typename, typename> class Strategy,
             typename Key,
             typename Value>
    class AstraCache {
    public:
        template<typename... Args>
        AstraCache(Args &&...args)
            : strategy_(std::forward<Args>(args)...) {}

        std::optional<Value> Get(const Key &key) {
            return strategy_.Get(key);
        }

        void Put(const Key &key, const Value &value, std::chrono::seconds ttl = std::chrono::seconds::zero()) {
            strategy_.Put(key, value, ttl);
        }

        std::optional<std::chrono::seconds> GetExpiryTime(const Key &key) const {
            return strategy_.GetExpiryTime(key);
        }

        std::vector<Key> GetKeys() const {
            return strategy_.GetKeys();
        }

        std::vector<Value> GetValues() const {
            return strategy_.GetValues();
        }


        void Clear() {
            strategy_.Clear();
        }

        bool Remove(const Key &key) {
            return strategy_.Remove(key);
        }

        bool Contains(const Key &key) const {
            return strategy_.Contains(key);
        }

        size_t Size() const {
            return strategy_.Size();
        }

        size_t Capacity() const {
            return strategy_.Capacity();
        }

        std::vector<std::pair<Key, Value>> GetAllEntries() const {
            return strategy_.GetAllEntries();
        }

    private:
        Strategy<Key, Value> strategy_;
    };
}// namespace Astra::datastructures
