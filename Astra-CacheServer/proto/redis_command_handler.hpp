#pragma once
#include "CommandImpl.hpp"
#include "caching/AstraCacheStrategy.hpp"
#include <algorithm>
#include <cctype>
#include <concurrent/task_queue.hpp>
#include <datastructures/lru_cache.hpp>
#include <memory>
#include <string>
#include <utils/logger.hpp>
#include <vector>

namespace Astra::proto {
    using namespace Astra::datastructures;

    // 命令工厂
    class CommandFactory {
    public:
        explicit CommandFactory(std::shared_ptr<AstraCache<LRUCache, std::string,
                                                           std::string>>
                                        cache) : cache_(std::move(cache)) {}

        std::unique_ptr<ICommand> CreateCommand(const std::string &cmd) {
            if (cmd == "COMMAND") return std::make_unique<CommandCommand>();
            if (cmd == "INFO") return std::make_unique<InfoCommand>();
            if (cmd == "GET") return std::make_unique<GetCommand>(cache_);
            if (cmd == "SET") return std::make_unique<SetCommand>(cache_);
            if (cmd == "DEL") return std::make_unique<DelCommand>(cache_);
            if (cmd == "PING") return std::make_unique<PingCommand>();
            if (cmd == "KEYS") return std::make_unique<KeysCommand>(cache_);
            if (cmd == "TTL") return std::make_unique<TtlCommand>(cache_);
            if (cmd == "INCR") return std::make_unique<IncrCommand>(cache_);
            if (cmd == "DECR") return std::make_unique<DecrCommand>(cache_);
            if (cmd == "EXISTS") return std::make_unique<ExistsCommand>(cache_);
            if (cmd == "MGET") return std::make_unique<MGetCommand>(cache_);
            if (cmd == "MSET") return std::make_unique<MSetCommand>(cache_);
            return nullptr;
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string,
                                   std::string>>
                cache_;
    };

    // RedisCommandHandler 保持接口不变，内部用工厂和命令模式实现
    class RedisCommandHandler {
    public:
        explicit RedisCommandHandler(std::shared_ptr<AstraCache<LRUCache, std::string,
                                                                std::string>>
                                             cache)
            : factory_(std::move(cache)) {}

        std::string ProcessCommand(const std::vector<std::string> &argv) {
            if (argv.empty()) {
                return RespBuilder::SimpleString("ERR empty command");
            }

            std::string cmd = argv[0];
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::toupper(c); });

            ZEN_LOG_DEBUG("Processing command: {}", cmd);

            auto command = factory_.CreateCommand(cmd);
            if (!command) {
                return RespBuilder::SimpleString("ERR unknown command '" + cmd + "'");
            }

            return command->Execute(argv);
        }

    private:
        CommandFactory factory_;
    };
}// namespace Astra::proto