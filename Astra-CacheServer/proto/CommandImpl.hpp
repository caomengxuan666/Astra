#pragma once
#include "CommandResponseBuilder"
#include "ICommand.hpp"
#include "resp_builder.hpp"
#include <chrono>
#include <datastructures/lru_cache.hpp>
#include <memory>

namespace Astra::proto {
    using namespace datastructures;

    class GetCommand : public ICommand {
    public:
        explicit GetCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache) : cache_(std::move(cache)) {}
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2) {
                return "-ERR wrong number of arguments for 'GET'\r\n";
            }
            auto val = cache_->Get(argv[1]);
            if (!val) {
                return RespBuilder::Nil();
            }
            return RespBuilder::BulkString(*val);
        }

    private:
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

    class SetCommand : public ICommand {
    public:
        explicit SetCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache) : cache_(std::move(cache)) {}
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 3) return RespBuilder::SimpleString("ERR wrong number of arguments for 'SET'");

            std::chrono::seconds ttl(0);

            if (argv.size() >= 5 && (argv[3] == "EX" || argv[3] == "ex")) {
                try {
                    ttl = std::chrono::seconds(std::stoi(argv[4]));
                } catch (...) {
                    return RespBuilder::SimpleString("ERR invalid expire time");
                }
                cache_->Put(argv[1], argv[2], ttl);
            } else if (argv.size() == 3) {
                cache_->Put(argv[1], argv[2], ttl);
            } else {
                return RespBuilder::SimpleString("ERR syntax error");
            }
            return RespBuilder::SimpleString("OK");
        }

    private:
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

    class DelCommand : public ICommand {
    public:
        explicit DelCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache) : cache_(std::move(cache)) {}
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2) return "-ERR wrong number of arguments for 'DEL'\r\n";

            size_t count = 0;
            for (size_t i = 1; i < argv.size(); ++i) {
                if (cache_->Remove(argv[i])) ++count;
            }
            return RespBuilder::Integer(count);
        }

    private:
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

    class PingCommand : public ICommand {
    public:
        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() == 1) return Astra::proto::RespBuilder::SimpleString("PONG");
            else if (argv.size() == 2)
                return "+" + argv[1] + "\r\n";
            else
                return "-ERR wrong number of arguments for 'PING'\r\n";
        }
    };

    // 重构COMMAND命令使用CommandResponseBuilder
    class CommandCommand : public ICommand {
    public:
        CommandCommand() = default;

        std::string Execute(const std::vector<std::string> &argv) override {
            std::vector<CommandInfo> commands = {
                    {"GET", 2, {"readonly", "fast"}, 1, 1, 1, 0, "string"},
                    {"SET", -3, {"write"}, 1, 1, 1, 0, "string"},
                    {"DEL", -2, {"write"}, 1, 1, 1, 0, "keyspace"},
                    {"PING", 1, {"readonly", "fast"}, 0, 0, 0, 0, "connection"},
                    {"COMMAND", 0, {"readonly", "admin"}, 0, 0, 0, 0, "server"},
                    {"INFO", -1, {"readonly"}, 0, 0, 0, 0, "server"}};

            return CommandResponseBuilder::BuildCommandListResponse(commands);
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
        explicit KeysCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() < 2 || argv[1] != "*") {
                return Astra::proto::RespBuilder::SimpleString("ERR this implementation only supports 'KEYS *'");
            }

            auto allKeys = cache_->GetKeys();
            std::vector<std::string> bulkStrings;
            for (const auto &key: allKeys) {
                bulkStrings.push_back(Astra::proto::RespBuilder::BulkString(key));
            }

            return Astra::proto::RespBuilder::Array(bulkStrings);
        }

    private:
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

    class TtlCommand : public ICommand {
    public:
        explicit TtlCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::SimpleString("ERR wrong number of arguments for 'TTL'");
            }

            const std::string &key = argv[1];

            // 键不存在或已过期
            if (!cache_->HasKey(key)) {
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
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

    class IncrCommand : public ICommand {
    public:
        explicit IncrCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::SimpleString("ERR wrong number of arguments for 'INCR'");
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
                return RespBuilder::SimpleString("ERR value is not an integer or out of range");
            }
            if (*end != '\0') {
                return RespBuilder::SimpleString("ERR value is not an integer or out of range");
            }

            current += 1;
            cache_->Put(key, std::to_string(current));
            return RespBuilder::Integer(current);
        }

    private:
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

    class DecrCommand : public ICommand {
    public:
        explicit DecrCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::SimpleString("ERR wrong number of arguments for 'DECR'");
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
                return RespBuilder::SimpleString("ERR value is not an integer or out of range");
            }
            if (*end != '\0') {
                return RespBuilder::SimpleString("ERR value is not an integer or out of range");
            }

            current -= 1;
            cache_->Put(key, std::to_string(current));
            return RespBuilder::Integer(current);
        }

    private:
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

    class ExistsCommand : public ICommand {
    public:
        explicit ExistsCommand(std::shared_ptr<LRUCache<std::string, std::string>> cache)
            : cache_(std::move(cache)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            if (argv.size() != 2) {
                return RespBuilder::SimpleString("ERR wrong number of arguments for 'EXISTS'");
            }

            const std::string &key = argv[1];

            bool exists = cache_->HasKey(key);
            return RespBuilder::Integer(exists ? 1 : 0);
        }

    private:
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
    };

}// namespace Astra::proto
