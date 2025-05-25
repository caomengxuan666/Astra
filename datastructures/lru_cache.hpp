#pragma once

#include <list>
#include <optional>
#include <unordered_map>

namespace Astra::datastructures {

    template<typename Key, typename Value>
    class LRUCache {
    public:
        explicit LRUCache(size_t capacity, size_t hot_key_threshold = 100)
            : capacity_(capacity), hot_key_threshold_(hot_key_threshold) {}

        std::optional<Value> Get(const Key &key) {
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                return std::nullopt;
            }
            // 移动到头部
            if (usage_.begin() != it->second) {
                usage_.splice(usage_.begin(), usage_, it->second);
                cache_[key] = usage_.begin();
            }

            UpdateHotKey(key, it->second);
            return it->second->second;
        }

        void move_to_front(const Key &key) {
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                return;
            }
            usage_.splice(usage_.begin(), usage_, it->second);
            cache_[key] = usage_.begin();
        }

        void Put(const Key &key, const Value &value) {
            if (capacity_ == 0) return;

            auto it = cache_.find(key);
            if (it != cache_.end()) {
                // 更新已有键
                // 移动到头部
                usage_.splice(usage_.begin(), usage_, it->second);

                // 更新值
                it->second->second = value;

                // 更新 map 中的 iterator
                cache_[key] = usage_.begin();

                UpdateHotKey(key, it->second);

                return;
            }

            // 插入新项
            if (cache_.size() >= capacity_) {
                const auto &lru_key = usage_.back().first;
                cache_.erase(lru_key);
                usage_.pop_back();
            }

            usage_.push_front({key, value});
            cache_[key] = usage_.begin();
        }

        [[nodiscard]] bool Contains(const Key &key) const {
            return cache_.find(key) != cache_.end();
        }

        [[nodiscard]] size_t Size() const {
            return cache_.size();
        }

        [[nodiscard]] size_t Capacity() const {
            return capacity_;
        }

    private:
        size_t capacity_;
        size_t hot_key_threshold_;
        std::list<std::pair<Key, Value>> usage_;
        std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> cache_;
        mutable std::unordered_map<Key, size_t> access_count_;
        struct HotKeyCache {
            std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> hot_keys;
        } hot_key_cache_;

        void UpdateHotKey(const Key &key, const typename std::list<std::pair<Key, Value>>::iterator &it) {
            access_count_[key]++;
            if (access_count_[key] >= hot_key_threshold_) {
                // 将键移入热点缓存
                if (hot_key_cache_.hot_keys.find(key) == hot_key_cache_.hot_keys.end()) {
                    hot_key_cache_.hot_keys[key] = it;
                }
                // 重置访问计数
                access_count_.erase(key);
            }
        }
    };
}// namespace Astra::datastructures