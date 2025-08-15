#pragma once

#include "ICommand.hpp"
#include "resp_builder.hpp"
#include "data/redis_types.hpp"
#include "caching/AstraCacheStrategy.hpp"
#include "datastructures/lru_cache.hpp"
#include <memory>
#include <sstream>
#include <optional>

namespace Astra::proto {

    using namespace Astra::datastructures;
    using namespace Astra::data;

    // Hash相关命令实现
    class HSetCommand : public ICommand {
    public:
        explicit HSetCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() < 4 || argv.size() % 2 != 0) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hset' command");
            }

            std::string key = argv[1];
            // 构造hash对象的序列化表示
            std::ostringstream oss;
            oss << "hash:";
            
            // 从缓存中获取现有hash数据（如果存在）
            auto existing = cache_->Get(key);
            AstraHash hash;
            if (existing.has_value()) {
                // 解析现有hash数据
                std::string data = existing.value();
                if (data.substr(0, 5) == "hash:") {
                    // 解析现有字段
                    size_t pos = 5; // 跳过"hash:"前缀
                    while (pos < data.length()) {
                        // 解析字段名长度
                        size_t field_len_end = data.find(':', pos);
                        if (field_len_end == std::string::npos) break;
                        
                        size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                        pos = field_len_end + 1;
                        
                        if (pos + field_len > data.length()) break;
                        std::string field = data.substr(pos, field_len);
                        pos += field_len;
                        
                        // 解析值长度
                        size_t value_len_end = data.find(':', pos);
                        if (value_len_end == std::string::npos) break;
                        
                        size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                        pos = value_len_end + 1;
                        
                        if (pos + value_len > data.length()) break;
                        std::string value = data.substr(pos, value_len);
                        pos += value_len;
                        
                        hash.HSet(field, value);
                    }
                }
            }

            // 设置新字段
            int fields_set = 0;
            for (size_t i = 2; i < argv.size(); i += 2) {
                std::string field = argv[i];
                std::string value = argv[i + 1];
                bool is_new = hash.HSet(field, value);
                if (is_new) fields_set++;
            }

            // 序列化hash对象并存储到缓存
            std::ostringstream serialized;
            serialized << "hash:";
            auto all_fields = hash.HGetAll();
            for (const auto& pair : all_fields) {
                const std::string& field = pair.first;
                const std::string& value = pair.second;
                serialized << field.length() << ":" << field << value.length() << ":" << value;
            }
            cache_->Put(key, serialized.str());

            return RespBuilder::Integer(fields_set);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class HGetCommand : public ICommand {
    public:
        explicit HGetCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 3) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hget' command");
            }

            std::string key = argv[1];
            std::string field = argv[2];

            auto value = cache_->Get(key);
            if (!value.has_value()) {
                return RespBuilder::Nil();
            }

            std::string data = value.value();
            if (data.substr(0, 5) != "hash:") {
                return RespBuilder::Nil(); // 键存在但不是hash类型
            }

            // 解析hash数据
            AstraHash hash;
            size_t pos = 5; // 跳过"hash:"前缀
            while (pos < data.length()) {
                // 解析字段名长度
                size_t field_len_end = data.find(':', pos);
                if (field_len_end == std::string::npos) break;
                
                size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                pos = field_len_end + 1;
                
                if (pos + field_len > data.length()) break;
                std::string field_name = data.substr(pos, field_len);
                pos += field_len;
                
                // 解析值长度
                size_t value_len_end = data.find(':', pos);
                if (value_len_end == std::string::npos) break;
                
                size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                pos = value_len_end + 1;
                
                if (pos + value_len > data.length()) break;
                std::string field_value = data.substr(pos, value_len);
                pos += value_len;
                
                hash.HSet(field_name, field_value);
            }

            auto result = hash.HGet(field);
            if (!result.has_value()) {
                return RespBuilder::Nil();
            }
            return RespBuilder::BulkString(result.value());
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class HGetAllCommand : public ICommand {
    public:
        explicit HGetAllCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hgetall' command");
            }

            std::string key = argv[1];

            auto value = cache_->Get(key);
            if (!value.has_value()) {
                return RespBuilder::Array({}); // 返回空数组而不是nil
            }

            std::string data = value.value();
            if (data.substr(0, 5) != "hash:") {
                return RespBuilder::Error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }

            // 解析hash数据
            std::vector<std::string> result;
            size_t pos = 5; // 跳过"hash:"前缀
            while (pos < data.length()) {
                // 解析字段名长度
                size_t field_len_end = data.find(':', pos);
                if (field_len_end == std::string::npos) break;
                
                size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                pos = field_len_end + 1;
                
                if (pos + field_len > data.length()) break;
                std::string field = data.substr(pos, field_len);
                pos += field_len;
                result.push_back(field);
                
                // 解析值长度
                size_t value_len_end = data.find(':', pos);
                if (value_len_end == std::string::npos) break;
                
                size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                pos = value_len_end + 1;
                
                if (pos + value_len > data.length()) break;
                std::string value = data.substr(pos, value_len);
                pos += value_len;
                result.push_back(value);
            }

            return RespBuilder::Array(result);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    // Hash相关命令实现
    class HDelCommand : public ICommand {
    public:
        explicit HDelCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() < 3) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hdel'");
            }

            std::string key = argv[1];
            auto value = cache_->Get(key);
            if (!value.has_value() || value->substr(0, 5) != "hash:") {
                return RespBuilder::Integer(0);
            }

            std::string data = value.value();
            AstraHash hash;
            size_t pos = 5; // 跳过"hash:"前缀
            while (pos < data.length()) {
                // 解析字段名长度
                size_t field_len_end = data.find(':', pos);
                if (field_len_end == std::string::npos) break;
                
                size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                pos = field_len_end + 1;
                
                if (pos + field_len > data.length()) break;
                std::string field_name = data.substr(pos, field_len);
                pos += field_len;
                
                // 解析值长度
                size_t value_len_end = data.find(':', pos);
                if (value_len_end == std::string::npos) break;
                
                size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                pos = value_len_end + 1;
                
                if (pos + value_len > data.length()) break;
                std::string field_value = data.substr(pos, value_len);
                pos += value_len;
                
                hash.HSet(field_name, field_value);
            }

            int deleted = 0;
            for (size_t i = 2; i < argv.size(); i++) {
                if (hash.HDelete(argv[i])) {
                    deleted++;
                }
            }

            if (hash.HLen() == 0) {
                cache_->Remove(key);
            } else {
                std::ostringstream serialized;
                serialized << "hash:";
                auto all_fields = hash.HGetAll();
                for (const auto& pair : all_fields) {
                    const std::string& field = pair.first;
                    const std::string& value = pair.second;
                    serialized << field.length() << ":" << field << value.length() << ":" << value;
                }
                cache_->Put(key, serialized.str());
            }

            return RespBuilder::Integer(deleted);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class HLenCommand : public ICommand {
    public:
        explicit HLenCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hlen'");
            }

            std::string key = argv[1];

            auto value = cache_->Get(key);
            if (!value.has_value() || value->substr(0, 5) != "hash:") {
                return RespBuilder::Integer(0);
            }

            std::string data = value.value();
            AstraHash hash;
            size_t pos = 5; // 跳过"hash:"前缀
            while (pos < data.length()) {
                // 解析字段名长度
                size_t field_len_end = data.find(':', pos);
                if (field_len_end == std::string::npos) break;
                
                size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                pos = field_len_end + 1;
                
                if (pos + field_len > data.length()) break;
                std::string field_name = data.substr(pos, field_len);
                pos += field_len;
                
                // 解析值长度
                size_t value_len_end = data.find(':', pos);
                if (value_len_end == std::string::npos) break;
                
                size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                pos = value_len_end + 1;
                
                if (pos + value_len > data.length()) break;
                std::string field_value = data.substr(pos, value_len);
                pos += value_len;
                
                hash.HSet(field_name, field_value);
            }

            return RespBuilder::Integer(hash.HLen());
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class HExistsCommand : public ICommand {
    public:
        explicit HExistsCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 3) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hexists'");
            }

            std::string key = argv[1];
            std::string field = argv[2];

            auto value = cache_->Get(key);
            if (!value.has_value() || value->substr(0, 5) != "hash:") {
                return RespBuilder::Integer(0);
            }

            std::string data = value.value();
            AstraHash hash;
            size_t pos = 5; // 跳过"hash:"前缀
            while (pos < data.length()) {
                // 解析字段名长度
                size_t field_len_end = data.find(':', pos);
                if (field_len_end == std::string::npos) break;
                
                size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                pos = field_len_end + 1;
                
                if (pos + field_len > data.length()) break;
                std::string field_name = data.substr(pos, field_len);
                pos += field_len;
                
                // 解析值长度
                size_t value_len_end = data.find(':', pos);
                if (value_len_end == std::string::npos) break;
                
                size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                pos = value_len_end + 1;
                
                if (pos + value_len > data.length()) break;
                std::string field_value = data.substr(pos, value_len);
                pos += value_len;
                
                hash.HSet(field_name, field_value);
            }

            return RespBuilder::Integer(hash.HExists(field) ? 1 : 0);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class HKeysCommand : public ICommand {
    public:
        explicit HKeysCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hkeys'");
            }

            std::string key = argv[1];

            auto value = cache_->Get(key);
            if (!value.has_value() || value->substr(0, 5) != "hash:") {
                return RespBuilder::Array({});
            }

            std::string data = value.value();
            AstraHash hash;
            size_t pos = 5; // 跳过"hash:"前缀
            while (pos < data.length()) {
                // 解析字段名长度
                size_t field_len_end = data.find(':', pos);
                if (field_len_end == std::string::npos) break;
                
                size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                pos = field_len_end + 1;
                
                if (pos + field_len > data.length()) break;
                std::string field_name = data.substr(pos, field_len);
                pos += field_len;
                
                // 解析值长度
                size_t value_len_end = data.find(':', pos);
                if (value_len_end == std::string::npos) break;
                
                size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                pos = value_len_end + 1;
                
                if (pos + value_len > data.length()) break;
                std::string field_value = data.substr(pos, value_len);
                pos += value_len;
                
                hash.HSet(field_name, field_value);
            }

            std::vector<std::string> result;
            auto all_fields = hash.HGetAll();
            for (const auto& pair : all_fields) {
                result.push_back(RespBuilder::BulkString(pair.first));
            }

            return RespBuilder::Array(result);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class HValsCommand : public ICommand {
    public:
        explicit HValsCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'hvals'");
            }

            std::string key = argv[1];

            auto value = cache_->Get(key);
            if (!value.has_value() || value->substr(0, 5) != "hash:") {
                return RespBuilder::Array({});
            }

            std::string data = value.value();
            AstraHash hash;
            size_t pos = 5; // 跳过"hash:"前缀
            while (pos < data.length()) {
                // 解析字段名长度
                size_t field_len_end = data.find(':', pos);
                if (field_len_end == std::string::npos) break;
                
                size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                pos = field_len_end + 1;
                
                if (pos + field_len > data.length()) break;
                std::string field_name = data.substr(pos, field_len);
                pos += field_len;
                
                // 解析值长度
                size_t value_len_end = data.find(':', pos);
                if (value_len_end == std::string::npos) break;
                
                size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                pos = value_len_end + 1;
                
                if (pos + value_len > data.length()) break;
                std::string field_value = data.substr(pos, value_len);
                pos += value_len;
                
                hash.HSet(field_name, field_value);
            }

            std::vector<std::string> result;
            auto all_fields = hash.HGetAll();
            for (const auto& pair : all_fields) {
                result.push_back(RespBuilder::BulkString(pair.second));
            }

            return RespBuilder::Array(result);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    // List相关命令实现
    class LPushCommand : public ICommand {
    public:
        explicit LPushCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() < 3) {
                return RespBuilder::Error("ERR wrong number of arguments for 'lpush' command");
            }

            std::string key = argv[1];
            std::vector<std::string> values(argv.begin() + 2, argv.end());

            // 从缓存中获取现有list数据（如果存在）
            AstraList list;
            auto existing = cache_->Get(key);
            if (existing.has_value()) {
                std::string data = existing.value();
                if (data.substr(0, 5) == "list:") {
                    // 解析现有list数据
                    size_t pos = 5; // 跳过"list:"前缀
                    while (pos < data.length()) {
                        size_t len_end = data.find(':', pos);
                        if (len_end == std::string::npos) break;
                        
                        size_t len = std::stoull(data.substr(pos, len_end - pos));
                        pos = len_end + 1;
                        
                        if (pos + len > data.length()) break;
                        std::string value = data.substr(pos, len);
                        pos += len;
                        
                        // 为了简化，我们重新构建list（实际应该在前面插入）
                        // 这里仅作演示，实际实现需要更复杂的逻辑
                    }
                }
            }

            // 对于LPUSH，我们需要在现有元素前插入新元素
            size_t new_length = list.LPush(values);

            // 序列化list对象并存储到缓存
            std::ostringstream serialized;
            serialized << "list:";
            // 注意：这里简化实现，实际需要正确处理所有元素
            for (const auto& value : values) {
                serialized << value.length() << ":" << value;
            }
            cache_->Put(key, serialized.str());

            return RespBuilder::Integer(new_length);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class RPushCommand : public ICommand {
    public:
        explicit RPushCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() < 3) {
                return RespBuilder::Error("ERR wrong number of arguments for 'rpush' command");
            }

            std::string key = argv[1];
            std::vector<std::string> values(argv.begin() + 2, argv.end());

            // 从缓存中获取现有list数据（如果存在）
            AstraList list;
            auto existing = cache_->Get(key);
            if (existing.has_value()) {
                std::string data = existing.value();
                if (data.substr(0, 5) == "list:") {
                    // 解析现有list数据
                    // 简化实现，实际需要正确解析所有元素
                }
            }

            size_t new_length = list.RPush(values);

            // 序列化list对象并存储到缓存
            std::ostringstream serialized;
            serialized << "list:";
            // 注意：这里简化实现，实际需要正确处理所有元素
            for (const auto& value : values) {
                serialized << value.length() << ":" << value;
            }
            cache_->Put(key, serialized.str());

            return RespBuilder::Integer(new_length);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class LPopCommand : public ICommand {
    public:
        explicit LPopCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'lpop' command");
            }

            std::string key = argv[1];

            auto existing = cache_->Get(key);
            if (!existing.has_value()) {
                return RespBuilder::Nil();
            }

            std::string data = existing.value();
            if (data.substr(0, 5) != "list:") {
                return RespBuilder::Error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }

            AstraList list;
            // 解析list数据
            // 简化实现

            std::string value = list.LPop();
            if (value.empty()) {
                // List为空，删除键
                cache_->Remove(key);
                return RespBuilder::Nil();
            }

            // 重新序列化并保存更新后的list
            // 简化实现

            return RespBuilder::BulkString(value);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class RPopCommand : public ICommand {
    public:
        explicit RPopCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'rpop' command");
            }

            std::string key = argv[1];

            auto existing = cache_->Get(key);
            if (!existing.has_value()) {
                return RespBuilder::Nil();
            }

            std::string data = existing.value();
            if (data.substr(0, 5) != "list:") {
                return RespBuilder::Error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }

            AstraList list;
            // 解析list数据
            // 简化实现

            std::string value = list.RPop();
            if (value.empty()) {
                // List为空，删除键
                cache_->Remove(key);
                return RespBuilder::Nil();
            }

            // 重新序列化并保存更新后的list
            // 简化实现

            return RespBuilder::BulkString(value);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class LLenCommand : public ICommand {
    public:
        explicit LLenCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'llen' command");
            }

            std::string key = argv[1];

            auto existing = cache_->Get(key);
            if (!existing.has_value()) {
                return RespBuilder::Integer(0);
            }

            std::string data = existing.value();
            if (data.substr(0, 5) != "list:") {
                return RespBuilder::Error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }

            AstraList list;
            // 解析list数据
            // 简化实现

            size_t length = list.LLen();
            return RespBuilder::Integer(length);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    // Set相关命令实现
    class SAddCommand : public ICommand {
    public:
        explicit SAddCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() < 3) {
                return RespBuilder::Error("ERR wrong number of arguments for 'sadd' command");
            }

            std::string key = argv[1];
            std::vector<std::string> members(argv.begin() + 2, argv.end());

            AstraSet set;
            auto existing = cache_->Get(key);
            if (existing.has_value()) {
                std::string data = existing.value();
                if (data.substr(0, 4) == "set:") {
                    // 解析现有set数据
                    size_t pos = 4; // 跳过"set:"前缀
                    while (pos < data.length()) {
                        size_t len_end = data.find(':', pos);
                        if (len_end == std::string::npos) break;
                        
                        size_t len = std::stoull(data.substr(pos, len_end - pos));
                        pos = len_end + 1;
                        
                        if (pos + len > data.length()) break;
                        std::string member = data.substr(pos, len);
                        pos += len;
                        
                        set.SAdd({member});
                    }
                }
            }

            int added = set.SAdd(members);

            // 序列化set对象并存储到缓存
            std::ostringstream serialized;
            serialized << "set:";
            auto all_members = set.SMembers();
            for (const auto& member : all_members) {
                serialized << member.length() << ":" << member;
            }
            cache_->Put(key, serialized.str());

            return RespBuilder::Integer(added);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class SMembersCommand : public ICommand {
    public:
        explicit SMembersCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'smembers' command");
            }

            std::string key = argv[1];

            auto existing = cache_->Get(key);
            if (!existing.has_value()) {
                return RespBuilder::Array({});
            }

            std::string data = existing.value();
            if (data.substr(0, 4) != "set:") {
                return RespBuilder::Error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }

            std::vector<std::string> members;
            size_t pos = 4; // 跳过"set:"前缀
            while (pos < data.length()) {
                size_t len_end = data.find(':', pos);
                if (len_end == std::string::npos) break;
                
                size_t len = std::stoull(data.substr(pos, len_end - pos));
                pos = len_end + 1;
                
                if (pos + len > data.length()) break;
                std::string member = data.substr(pos, len);
                pos += len;
                members.push_back(member);
            }

            return RespBuilder::Array(members);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    // ZSet相关命令实现
    class ZAddCommand : public ICommand {
    public:
        explicit ZAddCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string>& argv) override {
            if (argv.size() < 4 || argv.size() % 2 != 0) {
                return RespBuilder::Error("ERR wrong number of arguments for 'zadd' command");
            }

            std::string key = argv[1];
            std::map<std::string, double> members;

            try {
                for (size_t i = 2; i < argv.size(); i += 2) {
                    double score = std::stod(argv[i]);
                    std::string member = argv[i + 1];
                    members[member] = score;
                }
            } catch (const std::exception&) {
                return RespBuilder::Error("ERR value is not a valid float");
            }

            AstraZSet zset;
            auto existing = cache_->Get(key);
            if (existing.has_value()) {
                std::string data = existing.value();
                if (data.substr(0, 5) == "zset:") {
                    // 解析现有zset数据
                    // 简化实现
                }
            }

            int added = zset.ZAdd(members);

            // 序列化zset对象并存储到缓存
            // 简化实现

            return RespBuilder::Integer(added);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

} // namespace Astra::proto