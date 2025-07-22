#pragma once

#include "Astra-CacheServer/caching/AstraCacheStrategy.hpp"
#include "concurrent/task_queue.hpp"
#include <chrono>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>
namespace Astra::datastructures {


    template<typename Key, typename Value>
    class LRUCache : public AstraCacheStratgy<LRUCache<Key, Value>, Key, Value> {
    public:
        using iterator = typename std::list<std::pair<Key, Value>>::iterator;
        using const_iterator = typename std::list<std::pair<Key, Value>>::const_iterator;
        using clock_type = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_type>;

        explicit LRUCache(size_t capacity, size_t hot_key_threshold = 100, std::chrono::seconds ttl = std::chrono::seconds::zero())
            : capacity_(capacity), hot_key_threshold_(hot_key_threshold), ttl_(ttl) {}

        // 获取缓存中的值
        std::optional<Value> Get(const Key &key) {
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                return std::nullopt;
            }

            // 检查是否过期
            if (IsExpired(it)) {
                Remove(key);
                return std::nullopt;
            }

            MoveToFront(it);
            UpdateHotKey(it->second, key);
            return std::make_optional(it->second->second);
        }

        // 批量获取缓存中的值
        // 返回与输入keys顺序一致的values，不存在或过期的key对应的值为std::nullopt
        std::vector<std::optional<Value>> BatchGet(const std::vector<Key> &keys) {
            std::vector<std::optional<Value>> values;
            values.reserve(keys.size());

            for (const auto &key: keys) {
                auto it = cache_.find(key);
                if (it == cache_.end()) {
                    values.emplace_back(std::nullopt);
                    continue;
                }

                // 检查是否过期
                if (IsExpired(it)) {
                    Remove(key);
                    values.emplace_back(std::nullopt);
                    continue;
                }

                MoveToFront(it);
                UpdateHotKey(it->second, key);
                values.emplace_back(it->second->second);
            }

            return values;
        }

        void setCacheCapacity(size_t capacity) {
            capacity_ = capacity;
        }

        // 插入或更新缓存项
        void Put(const Key &key, const Value &value, std::chrono::seconds ttl = std::chrono::seconds::zero()) {
            if (capacity_ == 0) {
                Clear();
                return;
            }

            auto it = cache_.find(key);
            if (it != cache_.end()) {
                // 存在时删除旧元素
                usage_.erase(it->second);
                cache_.erase(it);
            }

            // 检查容量并淘汰LRU项
            EnsureCapacity(1);

            // 插入新元素
            usage_.emplace_front(key, value);
            cache_[key] = usage_.begin();
            UpdateHotKey(usage_.begin(), key);

            // 设置过期时间
            SetExpiration(key, ttl);
        }

        // 批量插入或更新缓存项
        // 注意：keys和values的大小必须相同
        void BatchPut(const std::vector<Key> &keys, const std::vector<Value> &values,
                      std::chrono::seconds ttl = std::chrono::seconds::zero()) {
            if (keys.size() != values.size()) {
                throw std::invalid_argument("keys and values must have the same size");
            }

            if (capacity_ == 0) {
                Clear();
                return;
            }

            // 计算需要淘汰的数量
            size_t required_capacity = keys.size();
            size_t current_size = cache_.size();
            size_t need_to_evict = 0;

            if (current_size + required_capacity > capacity_) {
                need_to_evict = current_size + required_capacity - capacity_;
            }

            // 批量淘汰LRU项
            EvictLRUBatch(need_to_evict);

            // 批量插入新元素
            for (size_t i = 0; i < keys.size(); ++i) {
                const auto &key = keys[i];
                const auto &value = values[i];

                auto it = cache_.find(key);
                if (it != cache_.end()) {
                    usage_.erase(it->second);
                    cache_.erase(it);
                }

                usage_.emplace_front(key, value);
                cache_[key] = usage_.begin();
                UpdateHotKey(usage_.begin(), key);
                SetExpiration(key, ttl);
            }
        }

        // 检查是否包含某个键
        [[nodiscard]] bool Contains(const Key &key) const {
            return cache_.find(key) != cache_.end();
        }

        // 获取当前缓存大小
        [[nodiscard]] size_t Size() const {
            return cache_.size();
        }

        // 获取容量
        [[nodiscard]] size_t Capacity() const {
            return capacity_;
        }

        // 获取所有缓存项（调试/监控用）
        std::vector<std::pair<Key, Value>> GetAllEntries() const {
            std::vector<std::pair<Key, Value>> result;
            for (auto it = usage_.begin(); it != usage_.end(); ++it) {
                result.emplace_back(it->first, it->second);
            }
            return result;
        }

        // 获取所有键
        std::vector<Key> GetKeys() const {
            std::vector<Key> keys;
            for (const auto &entry: usage_) {
                keys.emplace_back(entry.first);
            }
            return keys;
        }

        // 获取所有值
        std::vector<Value> GetValues() const {
            std::vector<Value> values;
            for (const auto &entry: usage_) {
                values.emplace_back(entry.second);
            }
            return values;
        }

        // 清空缓存
        void Clear() {
            usage_.clear();
            cache_.clear();
            access_count_.clear();
            hot_key_cache_.hot_keys.clear();
            expiration_times_.clear();
        }

        // 删除指定键
        bool Remove(const Key &key) {
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                return false;// 键不存在
            }

            // 从 usage_ 和 cache_ 中删除
            usage_.erase(it->second);
            cache_.erase(it);

            // 如果是热点键，也从热点缓存中移除
            auto hot_it = hot_key_cache_.hot_keys.find(key);
            if (hot_it != hot_key_cache_.hot_keys.end()) {
                hot_key_cache_.hot_keys.erase(hot_it);
            }

            // 移除访问计数和过期时间
            access_count_.erase(key);
            expiration_times_.erase(key);

            return true;
        }

        // 批量删除指定键
        size_t BatchRemove(const std::vector<Key> &keys) {
            size_t removed_count = 0;
            for (const auto &key: keys) {
                if (Remove(key)) {
                    removed_count++;
                }
            }
            return removed_count;
        }

        // 启动定期清理任务
        void StartEvictionTask(concurrent::TaskQueue &task_queue, std::chrono::seconds interval = std::chrono::seconds(1)) {
            eviction_task_queue_ = &task_queue;
            CleanUpExpiredItems();
            ScheduleNextCleanup(interval);
        }

        //  停止定期清理任务
        void StopEvictionTask() {
            eviction_active_ = false;
        }


        bool HasKey(const Key &key) const {
            auto it = cache_.find(key);
            if (it == cache_.end()) return false;
            return !IsExpired(it);
        }

        // 获取某个键的过期时间（如果存在）
        std::optional<std::chrono::seconds> GetExpiryTime(const Key &key) const {
            auto exp_it = expiration_times_.find(key);
            if (exp_it == expiration_times_.end()) return std::nullopt;

            auto now = std::chrono::steady_clock::now();
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(exp_it->second - now);
            return (remaining > std::chrono::seconds::zero()) ? std::make_optional(remaining) : std::nullopt;
        }

    protected:
        // 提取为 protected，便于子类扩展
        void MoveToFront(typename std::unordered_map<Key, iterator>::iterator it) {
            if (usage_.begin() != it->second) {
                usage_.splice(usage_.begin(), usage_, it->second);
                cache_[it->first] = usage_.begin();
            }
        }

        // 淘汰最近最少使用的项
        void EvictLRU() {
            if (usage_.empty()) return;

            const auto &lru_entry = usage_.back();
            cache_.erase(lru_entry.first);
            expiration_times_.erase(lru_entry.first);
            access_count_.erase(lru_entry.first);
            hot_key_cache_.hot_keys.erase(lru_entry.first);
            usage_.pop_back();
        }

        // 批量淘汰最近最少使用的项
        void EvictLRUBatch(size_t count) {
            if (count == 0 || usage_.empty()) return;

            // 限制最多淘汰的数量
            size_t evict_count = std::min(count, usage_.size());

            for (size_t i = 0; i < evict_count; ++i) {
                const auto &lru_entry = usage_.back();
                cache_.erase(lru_entry.first);
                expiration_times_.erase(lru_entry.first);
                access_count_.erase(lru_entry.first);
                hot_key_cache_.hot_keys.erase(lru_entry.first);
                usage_.pop_back();
            }
        }

        // 确保有足够的容量
        void EnsureCapacity(size_t required) {
            if (cache_.size() + required <= capacity_) return;

            size_t need_to_evict = cache_.size() + required - capacity_;
            EvictLRUBatch(need_to_evict);
        }

        // 设置过期时间
        void SetExpiration(const Key &key, std::chrono::seconds ttl) {
            if (ttl.count() > 0) {
                expiration_times_[key] = clock_type::now() + ttl;
            } else if (ttl_ > std::chrono::seconds::zero()) {
                expiration_times_[key] = clock_type::now() + ttl_;
            } else {
                expiration_times_.erase(key);
            }
        }

        // 更新热点键缓存
        void UpdateHotKey(iterator it, const Key &key) {
            access_count_[key]++;
            if (access_count_[key] >= hot_key_threshold_) {
                if (hot_key_cache_.hot_keys.find(key) == hot_key_cache_.hot_keys.end()) {
                    hot_key_cache_.hot_keys[key] = it;
                }
                access_count_.erase(key);
            }
        }

        // 获取底层迭代器（用于扩展）
        iterator GetIterator(const Key &key) {
            auto it = cache_.find(key);
            if (it == cache_.end()) return usage_.end();
            return it->second;
        }


    private:
        // 检查项是否过期
        bool IsExpired(const typename std::unordered_map<Key, iterator>::const_iterator &it) const {
            auto exp_it = expiration_times_.find(it->first);
            if (exp_it != expiration_times_.end()) {
                return exp_it->second <= clock_type::now();
            }
            return false;
        }

        // 清理过期项
        void CleanUpExpiredItems() {
            for (auto it = expiration_times_.begin(); it != expiration_times_.end();) {
                if (it->second <= clock_type::now()) {
                    Remove(it->first);
                    it = expiration_times_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // 安排下一次清理
        void ScheduleNextCleanup(std::chrono::seconds interval) {
            if (eviction_task_queue_ && eviction_active_) {
                eviction_task_queue_->Post([this, interval]() {
                    if (eviction_active_) {
                        CleanUpExpiredItems();
                        ScheduleNextCleanup(interval);
                    }
                });
            }
        }

        std::atomic<bool> eviction_active_{true};
        size_t capacity_;
        size_t hot_key_threshold_;
        std::chrono::seconds ttl_;
        concurrent::TaskQueue *eviction_task_queue_ = nullptr;
        std::unordered_map<Key, time_point> expiration_times_;
        std::list<std::pair<Key, Value>> usage_;
        std::unordered_map<Key, iterator> cache_;
        mutable std::unordered_map<Key, size_t> access_count_;
        struct HotKeyCache {
            std::unordered_map<Key, iterator> hot_keys;
        } hot_key_cache_;
    };

}// namespace Astra::datastructures
