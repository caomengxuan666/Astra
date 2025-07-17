#pragma once
#include "CommandResponseBuilder.hpp"
#include "ICommand.hpp"
#include "caching/AstraCacheStrategy.hpp"
#include "command_parser.hpp"
#include "resp_builder.hpp"
#include <chrono>
#include <datastructures/lru_cache.hpp>
#include <memory>

namespace Astra::proto {
    using namespace datastructures;

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
                    {"DECR", 2, {"write"}, 1, 1, 1, 0, "string", "Decrement the integer value of a key by one", "1.0.0", "O(1)", {}, {}},
                    {"EXISTS", 2, {"readonly"}, 1, 1, 1, 0, "keyspace", "Determine if a key exists", "1.0.0", "O(1)", {}, {}},
                    {"MGET", -2, {"readonly", "fast"}, 1, -1, 1, 0, "string", "Get the values of multiple keys", "1.0.0", "O(N)", {}, {}},
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

    // 重构INFO命令使用RespBuilder
    class InfoCommand : public ICommand {
    public:
        InfoCommand() = default;

        std::string Execute(const std::vector<std::string> &argv) override {
            std::string info =
                    "# Server\r\n"
                    "redis_version:6.0.9\r\n"
                    "os:linux\r\n"
                    "arch_bits:64\r\n"
                    "process_id:12345\r\n"
                    "uptime_in_seconds:3600\r\n"
                    "uptime_in_days:0\r\n"
                    "# Clients\r\n"
                    "connected_clients:1\r\n"
                    "# Memory\r\n"
                    "used_memory:1024000\r\n"
                    "# Stats\r\n"
                    "total_commands_processed:1000\r\n"
                    "total_connections_received:10\r\n";
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
            // 校验参数：至少需要1个键（argv[0]是"MGET"，argv[1..n]是键）
            if (argv.size() < 2) {
                return RespBuilder::Error("ERR wrong number of arguments for 'MGET'");
            }

            // 批量获取每个键的值
            std::vector<std::string> bulkValues;
            for (size_t i = 1; i < argv.size(); ++i) {
                const std::string &key = argv[i];
                auto val = cache_->Get(key);
                if (val) {
                    // 存在的键：返回BulkString
                    bulkValues.push_back(RespBuilder::BulkString(*val));
                } else {
                    // 不存在的键：返回Nil
                    bulkValues.push_back(RespBuilder::Nil());
                }
            }

            // 用数组包装所有值，返回RESP数组响应
            return RespBuilder::Array(bulkValues);
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

    class MSetCommand : public ICommand {
    public:
        explicit MSetCommand(std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            // 校验参数：至少需要 1 对键值对（参数总数为奇数，argv [0] 是 "MSET"）
            if (argv.size() < 3 || (argv.size() % 2) != 1) {
                return RespBuilder::Error("ERR wrong number of arguments for 'MSET'");
            }

            // 批量设置键值对（i 从 1 开始，步长为 2：i 是键，i+1 是值）
            for (size_t i = 1; i < argv.size(); i += 2) {
                const std::string &key = argv[i];
                const std::string &value = argv[i + 1];
                cache_->Put(key, value);
            }

            return RespBuilder::SimpleString("OK");
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
    };

}// namespace Astra::proto
