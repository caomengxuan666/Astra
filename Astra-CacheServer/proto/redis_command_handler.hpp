#pragma once
#include "CommandImpl.hpp"
#include "caching/AstraCacheStrategy.hpp"
#include "server/ChannelManager.hpp"// 引入频道管理器
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

    // 命令工厂：整合所有命令（包括 Pub/Sub）
    class CommandFactory {
    public:
        // 构造函数：接收缓存和频道管理器
        explicit CommandFactory(
                std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache,
                std::shared_ptr<apps::ChannelManager> channel_manager,
                std::weak_ptr<apps::Session> session// 新增：Session弱指针
                ) : cache_(std::move(cache)),
                    channel_manager_(std::move(channel_manager)),
                    session_(std::move(session)) {}// 保存session
        std::unique_ptr<ICommand> CreateCommand(const std::string &cmd) {
            // 缓存类命令（原有逻辑）
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

            // Pub/Sub 命令（新增逻辑）
            if (cmd == "PUBSUB") {
                return std::make_unique<PubSubCommand>(channel_manager_);
            }
            if (cmd == "SUBSCRIBE") return std::make_unique<SubscribeCommand>(channel_manager_);
            if (cmd == "UNSUBSCRIBE") return std::make_unique<UnsubscribeCommand>(channel_manager_, session_);
            if (cmd == "PUBLISH") return std::make_unique<PublishCommand>(channel_manager_);
            if (cmd == "PSUBSCRIBE") {
                return std::make_unique<PSubscribeCommand>(channel_manager_, session_);
            }
            if (cmd == "PUNSUBSCRIBE") {
                return std::make_unique<PUnsubscribeCommand>(channel_manager_, session_);
            }
            return nullptr;// 未知命令
        }

    private:
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
        std::shared_ptr<apps::ChannelManager> channel_manager_;// 新增：频道管理器
        std::weak_ptr<apps::Session> session_;                 // 新增：存储Session弱指针
    };

    // RedisCommandHandler：保持统一接口，自动处理所有命令（包括 Pub/Sub）
    class RedisCommandHandler {
    public:
        // 构造函数：传入缓存和频道管理器
        explicit RedisCommandHandler(
                std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache,
                std::shared_ptr<apps::ChannelManager> channel_manager,
                std::weak_ptr<apps::Session> session                                  // 新增：Session弱指针
                ) : factory_(std::move(cache), std::move(channel_manager), session) {}// 传递给factory

        std::string ProcessCommand(const std::vector<std::string> &argv) {
            if (argv.empty()) {
                return RespBuilder::Error("empty command");
            }

            // 命令大小写统一（Redis 命令不区分大小写）
            std::string cmd = argv[0];
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) {
                return std::toupper(c);
            });

            ZEN_LOG_DEBUG("Processing command: {}", cmd);

            // 统一创建命令并执行（包括 Pub/Sub 命令）
            auto command = factory_.CreateCommand(cmd);
            if (!command) {
                return RespBuilder::Error("unknown command '" + cmd + "'");
            }

            return command->Execute(argv);
        }

    private:
        CommandFactory factory_;// 工厂包含所有命令的创建逻辑
    };
}// namespace Astra::proto