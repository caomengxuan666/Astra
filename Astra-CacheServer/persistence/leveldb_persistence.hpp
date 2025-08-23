/**
 * @FilePath     : /Astra/Astra-CacheServer/persistence/leveldb_persistence.hpp
 * @Description  : 基于LevelDB的持久化实现
 * @Author       : Astra
 * @Version      : 0.0.1
 * @LastEditors  : Astra
 * @LastEditTime : 2025-08-23 14:36:22
 * @Copyright    : PESONAL DEVELOPER Astra., Copyright (c) 2025.
**/
#pragma once

#include "logger.hpp"
#include "util_path.hpp"
#include <chrono>
#include <datastructures/lru_cache.hpp>
#include <filesystem>

// 检查LevelDB是否可用
#if __has_include(<leveldb/db.h>)
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#define HAS_LEVELDB 1
#else
#define HAS_LEVELDB 0
#endif

namespace Astra::Persistence {
    namespace fs = std::filesystem;

#if HAS_LEVELDB
    /**
     * @brief 将缓存保存到LevelDB数据库
     * @tparam CacheStrategy 缓存策略模板
     * @tparam Key 键类型
     * @tparam Value 值类型
     * @param cache 要保存的缓存实例
     * @param db_path LevelDB数据库路径
     * @return 保存成功返回true，否则返回false
     */
    template<template<typename, typename> class CacheStrategy, typename Key, typename Value>
    bool SaveCacheToLevelDB(Astra::datastructures::AstraCache<CacheStrategy, Key, Value> &cache, const std::string &db_path) {
        // 确保目录存在
        if (!utils::ensureDirectoryExists(db_path)) {
            ZEN_LOG_ERROR("Cannot create directory for LevelDB: {}", db_path);
            return false;
        }

        leveldb::DB *db;
        leveldb::Options options;
        options.create_if_missing = true;

        leveldb::Status status = leveldb::DB::Open(options, db_path, &db);
        if (!status.ok()) {
            ZEN_LOG_ERROR("Failed to open LevelDB: {}", status.ToString());
            return false;
        }

        // 使用写入批次提高性能
        leveldb::WriteBatch batch;

        try {
            for (const auto &entry: cache.GetAllEntries()) {
                const Key &key = entry.first;
                const Value &value = entry.second;

                // 序列化键值对和过期时间
                auto expire_time_opt = cache.GetExpiryTime(key);
                int64_t expire_time = 0;
                if (expire_time_opt.has_value()) {
                    // GetExpiryTime返回的是std::chrono::seconds类型
                    expire_time = expire_time_opt.value().count() * 1000;// 转换为毫秒
                }

                // 构造存储值：value + expire_time
                std::string stored_value = value + "|" + std::to_string(expire_time);
                batch.Put(leveldb::Slice(key), leveldb::Slice(stored_value));
            }

            // 执行批量写入
            leveldb::WriteOptions write_options;
            write_options.sync = true;// 确保数据持久化
            status = db->Write(write_options, &batch);

            if (!status.ok()) {
                ZEN_LOG_ERROR("Failed to write to LevelDB: {}", status.ToString());
                delete db;
                return false;
            }

            delete db;
            ZEN_LOG_INFO("Successfully saved cache to LevelDB: {}", db_path);
            return true;
        } catch (const std::exception &e) {
            ZEN_LOG_ERROR("Exception while saving cache to LevelDB: {}", e.what());
            delete db;
            return false;
        }
    }

    /**
     * @brief 从LevelDB数据库加载缓存
     * @tparam CacheStrategy 缓存策略模板
     * @tparam Key 键类型
     * @tparam Value 值类型
     * @param cache 要加载的缓存实例
     * @param db_path LevelDB数据库路径
     * @return 加载成功返回true，否则返回false
     */
    template<template<typename, typename> class CacheStrategy, typename Key, typename Value>
    bool LoadCacheFromLevelDB(Astra::datastructures::AstraCache<CacheStrategy, Key, Value> &cache, const std::string &db_path) {
        ZEN_LOG_INFO("Loading cache from LevelDB: {}", db_path);

        if (!std::filesystem::exists(db_path)) {
            ZEN_LOG_WARN("LevelDB directory does not exist: {}", db_path);
            return false;
        }

        leveldb::DB *db;
        leveldb::Options options;

        leveldb::Status status = leveldb::DB::Open(options, db_path, &db);
        if (!status.ok()) {
            ZEN_LOG_ERROR("Failed to open LevelDB: {}", status.ToString());
            return false;
        }

        try {
            leveldb::Iterator *it = db->NewIterator(leveldb::ReadOptions());
            size_t loaded_count = 0;

            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                std::string key = it->key().ToString();
                std::string stored_value = it->value().ToString();

                // 解析存储的值
                size_t pos = stored_value.find_last_of('|');
                if (pos != std::string::npos) {
                    std::string value = stored_value.substr(0, pos);
                    int64_t expire_time = std::stoll(stored_value.substr(pos + 1));

                    // 恢复缓存项
                    if (expire_time > 0) {
                        // 使用std::chrono::seconds而不是time_point
                        cache.Put(key, value, std::chrono::seconds(expire_time / 1000));
                    } else {
                        cache.Put(key, value);
                    }
                    loaded_count++;
                }
            }

            if (!it->status().ok()) {
                ZEN_LOG_ERROR("Error iterating LevelDB: {}", it->status().ToString());
                delete it;
                delete db;
                return false;
            }

            delete it;
            delete db;

            ZEN_LOG_INFO("Loaded {} entries from LevelDB: {}", loaded_count, db_path);
            return true;
        } catch (const std::exception &e) {
            ZEN_LOG_ERROR("Exception while loading cache from LevelDB: {}", e.what());
            delete db;
            return false;
        }
    }
#else
    // 如果LevelDB不可用，提供空实现
    template<template<typename, typename> class CacheStrategy, typename Key, typename Value>
    bool SaveCacheToLevelDB(Astra::datastructures::AstraCache<CacheStrategy, Key, Value> &cache, const std::string &db_path) {
        ZEN_LOG_WARN("LevelDB support not available. Cannot save cache to LevelDB: {}", db_path);
        return false;
    }

    template<template<typename, typename> class CacheStrategy, typename Key, typename Value>
    bool LoadCacheFromLevelDB(Astra::datastructures::AstraCache<CacheStrategy, Key, Value> &cache, const std::string &db_path) {
        ZEN_LOG_WARN("LevelDB support not available. Cannot load cache from LevelDB: {}", db_path);
        return false;
    }
#endif
}// namespace Astra::Persistence