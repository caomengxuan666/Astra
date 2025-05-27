// commands.hpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>

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
                args.push_back("EX");
                args.push_back(std::to_string(ttl_.value().count()));
            }
            return args;
        }

    private:
        std::string key_;
        std::string value_;
        std::optional<std::chrono::seconds> ttl_;
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