#pragma once
#include "CommandResponseBuilder.hpp"
#include "ICommand.hpp"
#include "caching/AstraCacheStrategy.hpp"
#include "command_parser.hpp"
#include "resp_builder.hpp"
#include "server/ChannelManager.hpp"
#include "server/server_status.h"
#include "server/session.hpp"
#include <chrono>
#include <datastructures/lru_cache.hpp>
#include <memory>


namespace Astra::proto {
    using namespace datastructures;
    using namespace Astra::apps;

    class GetCommand : public ICommand {
    public:
        explicit GetCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache) : cache_(std::move(cache)) {}
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2) {
                return RespBuilder::Error("wrong number of arguments for 'GET'");
            }
            auto val = cache_->Get(argv[1]);
            if (!val) {
                return RespBuilder::Nil();
            }
            return RespBuilder::BulkString(*val);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class SetCommand : public ICommand {
    public:
        explicit SetCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache) : cache_(std::move(cache)) {}
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 3) return RespBuilder::Error("wrong number of arguments for 'SET'");

            std::chrono::seconds ttl(0);

            if (argv.size() >= 5 && (argv[3] == "EX" || argv[3] == "ex")) {
                try {
                    ttl = std::chrono::seconds(std::stoi(argv[4]));
                } catch (...) {
                    return RespBuilder::Error("invalid expire time");
                }
                cache_->Put(argv[1], argv[2], ttl);
            } else if (argv.size() == 3) {
                cache_->Put(argv[1], argv[2], ttl);
            } else {
                return RespBuilder::Error("syntax error");
            }
            return RespBuilder::SimpleString("OK");
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class DelCommand : public ICommand {
    public:
        explicit DelCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache) : cache_(std::move(cache)) {}
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2) return RespBuilder::Error("wrong number of arguments for 'DEL'");

            size_t count = 0;
            for (size_t i = 1; i < argv.size(); ++i) {
                if (cache_->Remove(argv[i])) ++count;
            }
            return RespBuilder::Integer(count);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class PingCommand : public ICommand {
    public:
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() == 1) {
                return RespBuilder::SimpleString("PONG");
            } else if (argv.size() == 2) {
                return RespBuilder::SimpleString(argv[1]);
            } else {
                return RespBuilder::Error("ERR wrong number of arguments for 'PING'");
            }
        }
    };

    // 重构COMMAND命令使用CommandResponseBuilder
    class CommandCommand : public ICommand {
    public:
        CommandCommand() = default;

        std::string Execute(const std::vector<std::string> &argv) override {
            std::vector<CommandInfo> commands = {
                    {"GET", 2, {"readonly", "fast"}, 1, 1, 1, 0, "string", "Get the value of a key", "1.0.0", "O(1)", {}, {}},
                    {"SET", -3, {"write"}, 1, 1, 1, 0, "string", "Set the string value of a key", "1.0.0", "O(1)", {}, {}},
                    {"DEL", -2, {"write"}, 1, 1, 1, 0, "keyspace", "Delete a key", "1.0.0", "O(N)", {}, {}},
                    {"PING", 1, {"readonly", "fast"}, 0, 0, 0, 0, "connection", "Ping the server", "1.0.0", "O(1)", {}, {}},
                    {"INFO", -1, {"readonly"}, 0, 0, 0, 0, "server", "Get information and statistics about the server", "1.0.0", "O(1)", {}, {}},
                    {"KEYS", -2, {"readonly"}, 1, 1, 1, 0, "keyspace", "Find all keys matching the given pattern", "1.0.0", "O(N)", {}, {}},
                    {"TTL", 2, {"readonly"}, 1, 1, 1, 0, "keyspace", "Get the time to live for a key", "1.0.0", "O(1)", {}, {}},
                    {"INCR", 2, {"write"}, 1, 1, 1, 0, "string", "Increment the integer value of a key by one", "1.0.0", "O(1)", {}, {}},
                    {"INCRBY", 3, {"write"}, 1, 1, 1, 0, "string", "Increment the integer value of a key by the given amount", "1.0.0", "O(1)", {}, {}},
                    {"DECR", 2, {"write"}, 1, 1, 1, 0, "string", "Decrement the integer value of a key by one", "1.0.0", "O(1)", {}, {}},
                    {"DECRBY", 3, {"write"}, 1, 1, 1, 0, "string", "Decrement the integer value of a key by the given amount", "1.0.0", "O(1)", {}, {}},
                    {"EXISTS", 2, {"readonly"}, 1, 1, 1, 0, "keyspace", "Determine if a key exists", "1.0.0", "O(1)", {}, {}},
                    {"MGET", -2, {"readonly", "fast"}, 1, -1, 1, 0, "string", "Get the values of multiple keys", "1.0.0", "O(N)", {}, {}},
                    {"MSET", -3, {"write"}, 1, 1, 1, 0, "string", "Set multiple keys to multiple values", "1.0.1", "O(N)", {}, {}},
                    {"HSET", -4, {"write", "fast"}, 1, 1, 1, 0, "hash", "Set the string value of a hash field", "2.0.0", "O(1)", {}, {}},
                    {"HGET", 3, {"readonly", "fast"}, 1, 1, 1, 0, "hash", "Get the value of a hash field", "2.0.0", "O(1)", {}, {}},
                    {"HGETALL", 2, {"readonly", "fast"}, 1, 1, 1, 0, "hash", "Get all the fields and values in a hash", "2.0.0", "O(N)", {}, {}},
                    {"HDEL", -3, {"write", "fast"}, 1, 1, 1, 0, "hash", "Delete one or more hash fields", "2.0.0", "O(N)", {}, {}},
                    {"HLEN", 2, {"readonly", "fast"}, 1, 1, 1, 0, "hash", "Get the number of fields in a hash", "2.0.0", "O(1)", {}, {}},
                    {"HEXISTS", 3, {"readonly", "fast"}, 1, 1, 1, 0, "hash", "Determine if a hash field exists", "2.0.0", "O(1)", {}, {}},
                    {"HKEYS", 2, {"readonly", "fast"}, 1, 1, 1, 0, "hash", "Get all the fields in a hash", "2.0.0", "O(N)", {}, {}},
                    {"HVALS", 2, {"readonly", "fast"}, 1, 1, 1, 0, "hash", "Get all the values in a hash", "2.0.0", "O(N)", {}, {}},
                    {"LPUSH", -3, {"write", "fast"}, 1, 1, 1, 0, "list", "Prepend one or multiple values to a list", "1.0.0", "O(1)", {}, {}},
                    {"RPUSH", -3, {"write", "fast"}, 1, 1, 1, 0, "list", "Append one or multiple values to a list", "1.0.0", "O(1)", {}, {}},
                    {"LPOP", -2, {"write", "fast"}, 1, 1, 1, 0, "list", "Remove and get the first element in a list", "1.0.0", "O(N)", {}, {}},
                    {"RPOP", -2, {"write", "fast"}, 1, 1, 1, 0, "list", "Remove and get the last element in a list", "1.0.0", "O(N)", {}, {}},
                    {"LLEN", 2, {"readonly", "fast"}, 1, 1, 1, 0, "list", "Get the length of a list", "1.0.0", "O(1)", {}, {}},
                    {"LRANGE", 4, {"readonly"}, 1, 1, 1, 0, "list", "Get a range of elements from a list", "1.0.0", "O(S+N)", {}, {}},
                    {"LINDEX", 3, {"readonly"}, 1, 1, 1, 0, "list", "Get an element from a list by its index", "1.0.0", "O(N)", {}, {}},
                    {"SADD", -3, {"write", "fast"}, 1, 1, 1, 0, "set", "Add one or more members to a set", "1.0.0", "O(1)", {}, {}},
                    {"SREM", -3, {"write", "fast"}, 1, 1, 1, 0, "set", "Remove one or more members from a set", "1.0.0", "O(1)", {}, {}},
                    {"SCARD", 2, {"readonly", "fast"}, 1, 1, 1, 0, "set", "Get the number of members in a set", "1.0.0", "O(1)", {}, {}},
                    {"SMEMBERS", 2, {"readonly", "fast"}, 1, 1, 1, 0, "set", "Get all the members in a set", "1.0.0", "O(N)", {}, {}},
                    {"SISMEMBER", 3, {"readonly", "fast"}, 1, 1, 1, 0, "set", "Determine if a given value is a member of a set", "1.0.0", "O(1)", {}, {}},
                    {"SPOP", 2, {"write", "fast"}, 1, 1, 1, 0, "set", "Remove and return one or multiple random members from a set", "1.0.0", "O(1)", {}, {}},
                    {"ZADD", -4, {"write", "fast"}, 1, 1, 1, 0, "zset", "Add one or more members to a sorted set, or update its score if it already exists", "1.2.0", "O(log(N))", {}, {}},
                    {"ZREM", -3, {"write", "fast"}, 1, 1, 1, 0, "zset", "Remove one or more members from a sorted set", "1.2.0", "O(log(N))", {}, {}},
                    {"ZCARD", 2, {"readonly", "fast"}, 1, 1, 1, 0, "zset", "Get the number of members in a sorted set", "1.2.0", "O(1)", {}, {}},
                    {"ZRANGE", -4, {"readonly"}, 1, 1, 1, 0, "zset", "Return a range of members in a sorted set", "1.2.0", "O(log(N)+M)", {}, {}},
                    {"ZRANGEBYSCORE", -4, {"readonly"}, 1, 1, 1, 0, "zset", "Return a range of members in a sorted set, by score", "1.2.0", "O(log(N)+M)", {}, {}},
                    {"ZSCORE", 3, {"readonly", "fast"}, 1, 1, 1, 0, "zset", "Get the score associated with the given member in a sorted set", "1.2.0", "O(1)", {}, {}},
                    {"COMMAND", 0, {"readonly", "admin"}, 0, 0, 0, 0, "server", "Get array of Redis command details", "2.8.13", "O(N)", {}, {}}};

            if (IsSubCommand(argv, "DOCS")) {
                std::vector<std::string> requestedCommands;
                for (size_t i = 2; i < argv.size(); ++i) {
                    requestedCommands.push_back(argv[i]);
                }
                return CommandResponseBuilder::BuildCommandDocsResponse(commands, requestedCommands);
            } else {
                return CommandResponseBuilder::BuildCommandListResponse(commands);
            }
        }
    };

    class InfoCommand : public ICommand {
    public:
        InfoCommand() = default;

        std::string Execute(const std::vector<std::string> &argv) override {
            // 获取服务器状态实例
            const auto &status = ServerStatusInstance::GetInstance()->getStatus();

            // 构建信息字符串，使用toCsr系列函数进行类型转换
            std::string info;
            info += "# Server\r\n";
            info += "server_name:";
            info += status.toCsr(status.server_name);
            info += "\r\n";
            info += "redis_version:";
            info += status.toCsr(status.version);
            info += "\r\n";
            info += "version_sha1:";
            info += status.toCsr(status.version_sha1);
            info += "\r\n";
            info += "build_id:";
            info += status.toCsr(status.build_id);
            info += "\r\n";
            info += "mode:";
            info += status.toCsr(status.mode);
            info += "\r\n";
            info += "os:";
            info += status.toCsr(status.os);
            info += "\r\n";
            info += "arch_bits:";
            info += status.toCsr(status.arch_bits);
            info += "\r\n";
            info += "process_id:";
            info += status.toCsr(status.process_id);
            info += "\r\n";
            info += "compiler_version:";
            info += status.toCsr(status.Compiler_version);
            info += "\r\n";
            info += "run_id:";
            info += status.toCsr(status.run_id);
            info += "\r\n";
            info += "tcp_port:";
            info += status.toCsr(status.tcp_port);
            info += "\r\n";
            info += "executable:";
            info += status.toCsr(status.executable);
            info += "\r\n";
            info += "config_file:";
            info += status.toCsr(status.config_file);
            info += "\r\n";
            info += "uptime_in_seconds:";
            info += status.toCsr(status.uptime_in_seconds);
            info += "\r\n";
            info += "uptime_in_days:";
            info += status.toCsr(status.uptime_in_days);
            info += "\r\n";

            info += "# Clients\r\n";
            info += "connected_clients:";
            info += status.toCsr(status.connected_clients);
            info += "\r\n";

            info += "# Memory\r\n";
            info += "used_memory:";
            info += status.toCsr(status.used_memory);
            info += "\r\n";
            info += "used_memory_human:";
            info += status.toCsr(status.used_memory_human);
            info += "\r\n";
            info += "used_memory_rss:";
            info += status.toCsr(status.used_memory_rss);
            info += "\r\n";
            info += "used_memory_rss_human:";
            info += status.toCsr(status.used_memory_rss_human);
            info += "\r\n";

            info += "# Stats\r\n";
            info += "total_connections_received:";
            info += status.toCsr(status.total_connections_received);
            info += "\r\n";
            info += "total_commands_processed:";
            info += status.toCsr(status.total_commands_processed);
            info += "\r\n";

            info += "# CPU\r\n";
            info += "used_cpu_sys:";
            info += status.toCsr(status.used_cpu_sys);
            info += "\r\n";
            info += "used_cpu_user:";
            info += status.toCsr(status.used_cpu_user);
            info += "\r\n";
            info += "used_cpu_sys_children:";
            info += status.toCsr(status.used_cpu_sys_children);
            info += "\r\n";
            info += "used_cpu_user_children:";
            info += status.toCsr(status.used_cpu_user_children);
            info += "\r\n";

            return Astra::proto::RespBuilder::BulkString(info);
        }
    };

    // 重构KEYS命令使用RespBuilder
    class KeysCommand : public ICommand {
    public:
        explicit KeysCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2 || argv[1] != "*") {
                return Astra::proto::RespBuilder::Error("ERR this implementation only supports 'KEYS *'");
            }

            auto allKeys = cache_->GetKeys();
            std::vector<std::string> bulkStrings;
            for (const auto &key: allKeys) {
                bulkStrings.push_back(Astra::proto::RespBuilder::BulkString(key));
            }

            return Astra::proto::RespBuilder::Array(bulkStrings);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class TtlCommand : public ICommand {
    public:
        explicit TtlCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'TTL'");
            }

            const std::string &key = argv[1];

            // 键不存在或已过期
            if (!cache_->Contains(key)) {
                return RespBuilder::Integer(0);
            }

            auto ttl = cache_->GetExpiryTime(key);
            if (!ttl.has_value()) {
                return RespBuilder::Integer(-1);
            }

            auto remaining = ttl.value();
            if (remaining.count() <= 0) {
                return RespBuilder::Integer(-2);
            }

            return RespBuilder::Integer(remaining.count());
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class IncrCommand : public ICommand {
    public:
        explicit IncrCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'INCR'");
            }

            const std::string &key = argv[1];
            auto val = cache_->Get(key);

            long long current = 0;
            if (!val) {
                // 键不存在，设置为 0 后加 1
                cache_->Put(key, "1");
                return RespBuilder::Integer(1);
            }

            char *end;
            errno = 0;
            current = std::strtoll(val->c_str(), &end, 10);
            if (errno == ERANGE || current < LLONG_MIN || current > LLONG_MAX) {
                return RespBuilder::Error("ERR value is not an integer or out of range");
            }
            if (*end != '\0') {
                return RespBuilder::Error("ERR value is not an integer or out of range");
            }

            current += 1;
            cache_->Put(key, std::to_string(current));
            return RespBuilder::Integer(current);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class DecrCommand : public ICommand {
    public:
        explicit DecrCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'DECR'");
            }

            const std::string &key = argv[1];
            auto val = cache_->Get(key);

            long long current = 0;
            if (!val) {
                // 键不存在，设置为 0 后减 1
                cache_->Put(key, "-1");
                return RespBuilder::Integer(-1);
            }

            char *end;
            errno = 0;
            current = std::strtoll(val->c_str(), &end, 10);
            if (errno == ERANGE || current < LLONG_MIN || current > LLONG_MAX) {
                return RespBuilder::Error("ERR value is not an integer or out of range");
            }
            if (*end != '\0') {
                return RespBuilder::Error("ERR value is not an integer or out of range");
            }

            current -= 1;
            cache_->Put(key, std::to_string(current));
            return RespBuilder::Integer(current);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class ExistsCommand : public ICommand {
    public:
        explicit ExistsCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'EXISTS'");
            }

            const std::string &key = argv[1];

            bool exists = cache_->Contains(key);
            return RespBuilder::Integer(exists ? 1 : 0);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class MGetCommand : public ICommand {
    public:
        explicit MGetCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            // 1. 校验参数格式
            if (argv.size() < 2) {
                ZEN_LOG_WARN("MGET invalid arguments: expected at least 1 key, got {}", argv.size() - 1);
                return RespBuilder::Error("ERR wrong number of arguments for 'MGET' command");
            }

            // 2. 提取输入键并记录数量
            const size_t key_count = argv.size() - 1;
            std::vector<std::string> keys;
            keys.reserve(key_count);
            for (size_t i = 1; i < argv.size(); ++i) {
                keys.push_back(argv[i]);
            }
            ZEN_LOG_DEBUG("MGET processing {} keys", key_count);

            // 3. 调用批量获取接口
            auto results = cache_->BatchGet(keys);

            // 4. 严格校验结果数量（核心修复点）
            if (results.size() != key_count) {
                ZEN_LOG_ERROR("MGET result count mismatch: expected {} results, got {}", key_count, results.size());
                return RespBuilder::Error("ERR MGET internal error: result count mismatch");
            }

            // 5. 构建响应元素（确保每个键对应一个结果）
            std::vector<std::string> bulk_values;
            bulk_values.reserve(key_count);// 预分配固定大小，避免扩容异常
            for (size_t i = 0; i < key_count; ++i) {
                const auto &val_opt = results[i];
                if (val_opt.has_value()) {
                    // 存在值：生成BulkString
                    bulk_values.push_back(RespBuilder::BulkString(val_opt.value()));
                } else {
                    // 不存在值：生成Nil
                    bulk_values.push_back(RespBuilder::Nil());
                }
            }

            // 6. 再次校验响应元素数量（双重保险）
            if (bulk_values.size() != key_count) {
                ZEN_LOG_ERROR("MGET response build mismatch: expected {} elements, built {}", key_count, bulk_values.size());
                return RespBuilder::Error("ERR MGET internal error: response build failed");
            }

            // 7. 生成最终数组响应
            std::string response = RespBuilder::Array(bulk_values);
            ZEN_LOG_DEBUG("MGET generated response (size: {} bytes) for {} keys", response.size(), key_count);
            return response;
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    // 修改后的MSetCommand
    class MSetCommand : public ICommand {
    public:
        explicit MSetCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            // 校验参数：至少需要 1 对键值对（参数总数为奇数，argv[0]是"MSET"）
            if (argv.size() < 3 || (argv.size() % 2) != 1) {
                return RespBuilder::Error("ERR wrong number of arguments for 'MSET'");
            }

            // 提取键和值到两个向量（预分配空间提升性能）
            size_t pair_count = (argv.size() - 1) / 2;
            std::vector<std::string> keys;
            std::vector<std::string> values;
            keys.reserve(pair_count);
            values.reserve(pair_count);

            for (size_t i = 1; i < argv.size(); i += 2) {
                keys.push_back(argv[i]);
                values.push_back(argv[i + 1]);
            }

            // 调用批量插入接口
            cache_->BatchPut(keys, values);

            return RespBuilder::SimpleString("OK");
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class SubscribeCommand : public ICommand {
    public:
        explicit SubscribeCommand(std::shared_ptr<ChannelManager> channel_manager)
            : channel_manager_(std::move(channel_manager)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2) {
                return RespBuilder::Error("SUBSCRIBE requires at least one channel");
            }

            // 记录本次订阅的频道（需要结合 Session 存储订阅关系，这里返回响应格式）
            std::unordered_set<std::string> channels;
            for (size_t i = 1; i < argv.size(); ++i) {
                channels.insert(argv[i]);
            }

            // 构建订阅响应（实际使用时需结合 Session 的 subscribed_channels_）
            return RespBuilder::SubscribeResponse(channels);
        }

    private:
        std::shared_ptr<ChannelManager> channel_manager_;// 频道管理器
    };

    // 取消订阅命令：UNSUBSCRIBE [channel [channel ...]]
    class UnsubscribeCommand : public ICommand {
    public:
        // 构造函数：需要频道管理器和当前会话（关键）
        explicit UnsubscribeCommand(
                std::shared_ptr<apps::ChannelManager> channel_manager,
                std::weak_ptr<apps::Session> session// 必须包含session参数
                ) : channel_manager_(std::move(channel_manager)),
                    session_(std::move(session)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            // 锁定会话（确保会话未过期）
            auto session = session_.lock();
            if (!session) {
                return RespBuilder::Error("Session expired");
            }

            std::unordered_set<std::string> unsubscribed;
            const auto &subscribed_channels = session->GetSubscribedChannels();

            if (argv.size() < 2) {
                // 无参数：取消所有订阅
                unsubscribed = subscribed_channels;
                for (const auto &channel: subscribed_channels) {
                    channel_manager_->Unsubscribe(channel, session);
                }
            } else {
                // 有参数：取消指定频道
                for (size_t i = 1; i < argv.size(); ++i) {
                    const std::string &channel = argv[i];
                    if (subscribed_channels.count(channel)) {
                        unsubscribed.insert(channel);
                        channel_manager_->Unsubscribe(channel, session);
                    }
                }
            }

            // 构建响应（包含实际取消的频道）
            return RespBuilder::UnsubscribeResponse(unsubscribed);
        }

    private:
        std::shared_ptr<apps::ChannelManager> channel_manager_;// 频道管理器
        std::weak_ptr<apps::Session> session_;                 // 当前会话（弱指针）
    };

    // 发布命令：PUBLISH channel message
    class PublishCommand : public ICommand {
    public:
        explicit PublishCommand(std::shared_ptr<ChannelManager> channel_manager)
            : channel_manager_(std::move(channel_manager)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 3) {
                return RespBuilder::Error("PUBLISH requires channel and message");
            }

            const std::string &channel = argv[1];
            const std::string &message = argv[2];
            size_t subscribers = channel_manager_->Publish(channel, message);// 发布消息并返回订阅者数量

            return RespBuilder::Integer(subscribers);// 返回接收消息的订阅者数量
        }

    private:
        std::shared_ptr<ChannelManager> channel_manager_;
    };

    // 模式订阅命令：PSUBSCRIBE pattern [pattern ...]
    class PSubscribeCommand : public ICommand {
    public:
        explicit PSubscribeCommand(
                std::shared_ptr<apps::ChannelManager> channel_manager,
                std::weak_ptr<apps::Session> session) : channel_manager_(std::move(channel_manager)),
                                                        session_(std::move(session)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            auto session = session_.lock();
            if (!session) {
                return RespBuilder::Error("Session expired");
            }

            if (argv.size() < 2) {
                return RespBuilder::Error("PSUBSCRIBE requires at least one pattern");
            }

            std::unordered_set<std::string> subscribed_patterns;
            for (size_t i = 1; i < argv.size(); ++i) {
                const std::string &pattern = argv[i];
                session->AddSubscribedPattern(pattern);
                channel_manager_->PSubscribe(pattern, session);
                subscribed_patterns.insert(pattern);
            }

            // 关键修复：在返回响应前切换到PubSub模式
            session->SwitchMode(SessionMode::PubSubMode);// 添加这一行

            // 构建并返回响应
            return RespBuilder::PSubscribeResponse(
                    subscribed_patterns,
                    session->GetSubscribedPatterns().size());
        }


    private:
        std::shared_ptr<apps::ChannelManager> channel_manager_;
        std::weak_ptr<apps::Session> session_;
    };

    // 取消模式订阅命令：PUNSUBSCRIBE [pattern [pattern ...]]
    class PUnsubscribeCommand : public ICommand {
    public:
        explicit PUnsubscribeCommand(
                std::shared_ptr<apps::ChannelManager> channel_manager,
                std::weak_ptr<apps::Session> session) : channel_manager_(std::move(channel_manager)),
                                                        session_(std::move(session)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            auto session = session_.lock();
            if (!session) {
                return RespBuilder::Error("Session expired");
            }

            std::unordered_set<std::string> unsubscribed_patterns;
            const auto &subscribed_patterns = session->GetSubscribedPatterns();// 调用接口获取

            if (argv.size() < 2) {
                // 无参数：取消所有模式订阅
                unsubscribed_patterns = subscribed_patterns;
                for (const auto &pattern: subscribed_patterns) {
                    channel_manager_->PUnsubscribe(pattern, session);
                }
                session->ClearSubscribedPatterns();// 调用接口清空
            } else {
                // 取消指定模式
                for (size_t i = 1; i < argv.size(); ++i) {
                    const std::string &pattern = argv[i];
                    if (subscribed_patterns.count(pattern)) {
                        unsubscribed_patterns.insert(pattern);
                        session->RemoveSubscribedPattern(pattern);// 调用接口移除
                        channel_manager_->PUnsubscribe(pattern, session);
                    }
                }
            }
            if (session->GetSubscribedPatterns().empty() && session->GetSubscribedChannels().empty()) {
                session->SwitchMode(SessionMode::CacheMode);
            }
            // 调用接口获取剩余订阅数
            return RespBuilder::PUnsubscribeResponse(
                    unsubscribed_patterns,
                    session->GetSubscribedPatterns().size());
        }

    private:
        std::shared_ptr<apps::ChannelManager> channel_manager_;
        std::weak_ptr<apps::Session> session_;
    };

    class PubSubCommand : public ICommand {
    public:
        explicit PubSubCommand(std::shared_ptr<apps::ChannelManager> channel_manager)
            : channel_manager_(std::move(channel_manager)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2) {
                return RespBuilder::Error("PUBSUB requires a subcommand (CHANNELS, NUMSUB, NUMPAT, PATTERNS)");
            }
            const std::string &subcmd = argv[1];// 获取子命令 (e.g., "channels", "CHANNELS")

            if (ICaseCmp(subcmd, "CHANNELS")) {
                return HandleChannels(argv);
            } else if (ICaseCmp(subcmd, "NUMSUB")) {
                return HandleNumSub(argv);
            } else if (ICaseCmp(subcmd, "NUMPAT")) {
                return HandleNumPat();
            } else if (ICaseCmp(subcmd, "PATTERNS")) {
                return HandlePatterns();
            } else {
                // 使用原始的 argv[1] 以便在错误信息中显示用户实际输入的内容
                return RespBuilder::Error("Unknown PUBSUB subcommand: " + argv[1]);
            }
        }

    private:
        // 处理 PUBSUB CHANNELS [pattern]
        std::string HandleChannels(const std::vector<std::string> &argv) {
            std::string pattern = "*";// 默认匹配所有频道
            if (argv.size() >= 3) {
                pattern = argv[2];
            }

            auto channels = channel_manager_->GetChannelsByPattern(pattern);
            std::vector<std::string> elements;
            for (const auto &channel: channels) {
                elements.push_back(RespBuilder::BulkString(channel));
            }
            return RespBuilder::Array(elements);
        }

        // 处理 PUBSUB NUMSUB channel1 [channel2 ...]
        std::string HandleNumSub(const std::vector<std::string> &argv) {
            if (argv.size() < 3) {
                return RespBuilder::Array({});// 无参数时返回空数组
            }

            std::vector<std::string> elements;
            for (size_t i = 2; i < argv.size(); ++i) {
                const std::string &channel = argv[i];
                size_t count = channel_manager_->GetChannelSubscriberCount(channel);
                elements.push_back(RespBuilder::BulkString(channel));
                elements.push_back(RespBuilder::Integer(count));
            }
            return RespBuilder::Array(elements);
        }

        // 处理 PUBSUB NUMPAT
        std::string HandleNumPat() {
            size_t count = channel_manager_->GetPatternSubscriberCount();
            return RespBuilder::Integer(count);
        }

        // 新增：处理 PUBSUB PATTERNS
        std::string HandlePatterns() {
            // 从ChannelManager获取所有活跃模式及订阅数
            auto active_patterns = channel_manager_->GetActivePatterns();
            std::vector<std::string> elements;

            for (const auto &[pattern, count]: active_patterns) {
                // 每个模式返回一个子数组：[模式名, 订阅数]
                std::vector<std::string> sub_elements;
                sub_elements.push_back(RespBuilder::BulkString(pattern));
                sub_elements.push_back(RespBuilder::Integer(count));
                elements.push_back(RespBuilder::Array(sub_elements));
            }

            return RespBuilder::Array(elements);
        }

    private:
        std::shared_ptr<apps::ChannelManager> channel_manager_;
    };
}// namespace Astra::proto
