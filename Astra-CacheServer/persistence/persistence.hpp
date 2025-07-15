/**
 * @FilePath     : /Astra/Astra-CacheServer/persistence/persistence.hpp
 * @Description  :  
 * @Author       : caomengxuan666 2507560089@qq.com
 * @Version      : 0.0.1
 * @LastEditors  : caomengxuan666 2507560089@qq.com
 * @LastEditTime : 2025-06-19 19:30:51
 * @Copyright    : PESONAL DEVELOPER CMX., Copyright (c) 2025.
**/
#include "util_path.hpp"
#include <chrono>
#include <datastructures/lru_cache.hpp>
#include <filesystem>
#include <fstream>
namespace Astra::Persistence {
    namespace fs = std::filesystem;
    using namespace Astra::datastructures;
    using namespace utils;
    using namespace std::chrono_literals;

    template<template<typename, typename> class CacheStrategy, typename Key, typename Value>
    bool SaveCacheToFile(AstraCache<CacheStrategy, Key, Value> &cache, const std::string &filename) {
        // 确保目录存在
        if (!ensureDirectoryExists(filename)) {
            ZEN_LOG_ERROR("Cannot create directory for file: {}", filename);
            return false;
        }

        ZEN_LOG_INFO("Saving cache to file: {}", filename);

        // 使用二进制模式写入，避免文本模式的转换问题
        std::ofstream out(filename, std::ios::binary);
        if (!out.is_open()) {
            ZEN_LOG_ERROR("Failed to open file for writing: {} - {}",
                          filename, strerror(errno));
            return false;
        }

        try {
            for (const auto &entry: cache.GetAllEntries()) {
                const Key &key = entry.first;
                const Value &value = entry.second;

                // 获取过期时间
                auto expire_time_opt = cache.GetExpiryTime(key);
                int64_t expire_time = 0;

                if (expire_time_opt.has_value()) {
                    expire_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          expire_time_opt.value())
                                          .count();
                }

                // 写入键值对和过期时间
                out << key << " " << value << " " << expire_time << "\n";
                ZEN_LOG_DEBUG("KEY: {} VALUE: {} EXPIRE_TIME: {}", key, value, expire_time);
            }

            // 检查写入是否成功
            if (!out.good()) {
                ZEN_LOG_ERROR("Error occurred while writing to file: {}", filename);
                out.close();
                fs::remove(filename);// 删除可能不完整的文件
                return false;
            }

            out.close();
            ZEN_LOG_INFO("Successfully saved cache to file: {}", filename);
            return true;
        } catch (const std::exception &e) {
            ZEN_LOG_ERROR("Exception while saving cache: {}", e.what());
            out.close();
            fs::remove(filename);// 删除可能不完整的文件
            return false;
        }
    }

    // 从文件恢复缓存
    template<template<typename, typename> class CacheStrategy, typename Key, typename Value>
    bool LoadCacheFromFile(AstraCache<CacheStrategy, Key, Value> &cache, const std::string &filename) {
        ZEN_LOG_INFO("Loading cache from file: {}", filename);

        // 检查文件是否存在
        if (!fs::exists(filename)) {
            ZEN_LOG_WARN("Cache file does not exist: {}", filename);
            return false;
        }

        std::ifstream in(filename, std::ios::binary);
        if (!in.is_open()) {
            ZEN_LOG_ERROR("Failed to open file for reading: {} - {}",
                          filename, strerror(errno));
            return false;
        }

        try {
            std::string line;
            size_t loaded_count = 0;
            size_t error_count = 0;

            while (std::getline(in, line)) {
                std::istringstream iss(line);
                Key key;
                Value value;
                int64_t expire_s = 0;

                if (!(iss >> key >> value >> expire_s)) {
                    ZEN_LOG_WARN("Failed to parse line: {}", line);
                    error_count++;
                    continue;
                }

                // 使用 Put 方法保证 LRU 正确性
                if (expire_s > 0) {
                    cache.Put(key, value, std::chrono::seconds(expire_s));
                } else {
                    cache.Put(key, value);
                }
                loaded_count++;
            }

            in.close();

            ZEN_LOG_INFO("Loaded {} entries from file: {} ({} errors)",
                         loaded_count, filename, error_count);
            return loaded_count > 0 || error_count == 0;
        } catch (const std::exception &e) {
            ZEN_LOG_ERROR("Exception while loading cache: {}", e.what());
            in.close();
            return false;
        }
    }
}// namespace Astra::Persistence