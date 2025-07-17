// commands.hpp
#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace Astra::Client::Command {


    // 命令基类
    class ICommand {
    public:
        virtual ~ICommand() = default;
        virtual std::vector<std::string> GetArgs() const = 0;
    };

    // PING命令
    class PingCommand : public ICommand {
    public:
        std::vector<std::string> GetArgs() const override {
            return {"PING"};
        }
    };

    // SET命令
    class SetCommand : public ICommand {
    public:
        SetCommand(const std::string &key, const std::string &value)
            : key_(key), value_(value), ttl_(std::nullopt) {}

        SetCommand(const std::string &key, const std::string &value, std::chrono::seconds ttl)
            : key_(key), value_(value), ttl_(ttl) {}

        std::vector<std::string> GetArgs() const override {
            std::vector<std::string> args = {"SET", key_, value_};
            if (ttl_.has_value()) {
                args.emplace_back("EX");
                args.emplace_back(std::to_string(ttl_.value().count()));
            }
            return args;
        }

    private:
        std::string key_;
        std::string value_;
        std::optional<std::chrono::seconds> ttl_;
    };

    // MSET命令
    class MSetCommand : public ICommand {
    public:
        // 构造函数：接收键值对列表（key1, value1, key2, value2...）
        explicit MSetCommand(const std::vector<std::pair<std::string, std::string>> &keyValues)
            : keyValues_(keyValues) {}

        // 生成MSET命令参数：{"MSET", "key1", "value1", "key2", "value2", ...}
        std::vector<std::string> GetArgs() const override {
            std::vector<std::string> args = {"MSET"};
            for (const auto &kv: keyValues_) {
                args.push_back(kv.first); // 添加键
                args.push_back(kv.second);// 添加值
            }
            return args;
        }

    private:
        std::vector<std::pair<std::string, std::string>> keyValues_;// 存储批量键值对
    };

    // GET命令
    class GetCommand : public ICommand {
    public:
        explicit GetCommand(const std::string &key) : key_(key) {}

        std::vector<std::string> GetArgs() const override {
            return {"GET", key_};
        }

    private:
        std::string key_;
    };

    // MGET命令
    class MGetCommand : public ICommand {
    public:
        explicit MGetCommand(const std::vector<std::string> &keys) : keys_(keys) {}

        std::vector<std::string> GetArgs() const override {
            std::vector<std::string> args = {"MGET"};
            args.insert(args.end(), keys_.begin(), keys_.end());// 拼接所有键
            return args;
        }

    private:
        std::vector<std::string> keys_;
    };

    // DEL命令
    class DelCommand : public ICommand {
    public:
        explicit DelCommand(const std::vector<std::string> &keys) : keys_(keys) {}

        std::vector<std::string> GetArgs() const override {
            std::vector<std::string> args = {"DEL"};
            args.insert(args.end(), keys_.begin(), keys_.end());
            return args;
        }

    private:
        std::vector<std::string> keys_;
    };

    // KEYS命令
    class KeysCommand : public ICommand {
    public:
        explicit KeysCommand(const std::string &pattern) : pattern_(pattern) {}

        std::vector<std::string> GetArgs() const override {
            return {"KEYS", pattern_};
        }

    private:
        std::string pattern_;
    };

    // TTL命令
    class TTLCommand : public ICommand {
    public:
        explicit TTLCommand(const std::string &key) : key_(key) {}

        std::vector<std::string> GetArgs() const override {
            return {"TTL", key_};
        }

    private:
        std::string key_;
    };

    // EXISTS命令
    class ExistsCommand : public ICommand {
    public:
        explicit ExistsCommand(const std::string &key) : key_(key) {}

        std::vector<std::string> GetArgs() const override {
            return {"EXISTS", key_};
        }

    private:
        std::string key_;
    };

    // INCR命令
    class IncrCommand : public ICommand {
    public:
        explicit IncrCommand(const std::string &key) : key_(key) {}

        std::vector<std::string> GetArgs() const override {
            return {"INCR", key_};
        }

    private:
        std::string key_;
    };

    // DECR命令
    class DecrCommand : public ICommand {
    public:
        explicit DecrCommand(const std::string &key) : key_(key) {}

        std::vector<std::string> GetArgs() const override {
            return {"DECR", key_};
        }

    private:
        std::string key_;
    };

}// namespace Astra::Client::Command