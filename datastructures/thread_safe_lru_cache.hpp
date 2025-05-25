#pragma once

#include "lru_cache.hpp"
#include <functional>
#include <mutex>
#include <vector>

namespace Astra::datastructures {

    template<typename Key, typename Value>
    struct Shard {
        LRUCache<Key, Value> cache;
        mutable std::mutex mutex;

        explicit Shard(size_t capacity) : cache(capacity) {}
    };

    template<typename Key, typename Value>
    class ThreadSafeLRUCache {
    public:
        explicit ThreadSafeLRUCache(size_t capacity, size_t num_shards = 8)
            : shards_(num_shards), shard_count_(num_shards), key_hash_() {
            for (auto &shard: shards_) {
                shard = std::make_unique<Shard<Key, Value>>(capacity / shard_count_);
            }
        }

        std::optional<Value> Get(const Key &key) {
            size_t index = key_hash_(key) % shard_count_;
            std::lock_guard<std::mutex> lock(shards_[index]->mutex);
            return shards_[index]->cache.Get(key);
        }

        void Put(const Key &key, const Value &value) {
            size_t index = key_hash_(key) % shard_count_;
            std::lock_guard<std::mutex> lock(shards_[index]->mutex);
            shards_[index]->cache.Put(key, value);
        }

        [[nodiscard]] bool Contains(const Key &key) const {
            size_t index = key_hash_(key) % shard_count_;
            std::lock_guard<std::mutex> lock(shards_[index]->mutex);
            return shards_[index]->cache.Contains(key);
        }

        [[nodiscard]] size_t Size() const {
            size_t total = 0;
            for (const auto &shard: shards_) {
                std::lock_guard<std::mutex> lock(shard->mutex);
                total += shard->cache.Size();
            }
            return total;
        }

        [[nodiscard]] size_t Capacity() const {
            size_t total = 0;
            for (const auto &shard: shards_) {
                std::lock_guard<std::mutex> lock(shard->mutex);
                total += shard->cache.Capacity();
            }
            return total;
        }

    private:
        std::vector<std::unique_ptr<Shard<Key, Value>>> shards_;
        size_t shard_count_;
        std::hash<Key> key_hash_;
    };
}// namespace Astra::datastructures