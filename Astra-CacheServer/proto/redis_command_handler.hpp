#pragma once
#include "CommandImpl.hpp"
// #include "LuaCommands.h" // 这个可能不需要，除非你定义了其他Lua特定的命令类
#include "LuaCommands.h"
#include "LuaExecutor.h"
#include "caching/AstraCacheStrategy.hpp"
#include "server/ChannelManager.hpp"// 引入频道管理器
#include "server/stats_event.h"
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

    // --- 新增：定义用于简化 Lua 命令注册的宏 ---
    // 宏定义：REGISTER_LUA_CACHE_COMMAND(lua_name, CppCommandClass)
    // 用于注册那些构造函数只需要 cache_ 的命令
#define REGISTER_LUA_CACHE_COMMAND(lua_name, CppCommandClass) \
    lua_executor_->RegisterCommandHandler(lua_name, std::make_shared<CppCommandClass>(cache_));

    // 宏定义：REGISTER_LUA_CHANNEL_COMMAND(lua_name, CppCommandClass)
    // 用于注册那些构造函数只需要 channel_manager_ 的命令
#define REGISTER_LUA_CHANNEL_COMMAND(lua_name, CppCommandClass) \
    lua_executor_->RegisterCommandHandler(lua_name, std::make_shared<CppCommandClass>(channel_manager_));

    // 如果需要同时传入 cache_ 和 channel_manager_，或者有其他参数组合，可以定义更多宏
#define REGISTER_LUA_CACHE_CHANNEL_COMMAND(lua_name, CppCommandClass) \
    lua_executor_->RegisterCommandHandler(lua_name, std::make_shared<CppCommandClass>(cache_, channel_manager_));


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
                    session_(std::move(session)) {// 移除重复的 lua_executor_ 初始化

            lua_executor_ = std::make_shared<LuaExecutor>(cache_);

            // --- 修改：调用初始化方法来注册 Lua 命令 ---
            InitializeLuaCommands();
            // --- 修改结束 ---
        }

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

            if (cmd == "EVAL") {
                return std::make_unique<EvalCommand>(lua_executor_);
            }
            if (cmd == "EVALSHA") {
                return std::make_unique<EvalShaCommand>(lua_executor_);
            }

            return nullptr;// 未知命令
        }

    private:
        // --- 新增：集中初始化需要在 Lua 中使用的命令 ---
        void InitializeLuaCommands() {
            // 使用宏来注册所有支持的、构造函数只需要 cache_ 的命令
            REGISTER_LUA_CACHE_COMMAND("get", GetCommand);
            REGISTER_LUA_CACHE_COMMAND("set", SetCommand);
            REGISTER_LUA_CACHE_COMMAND("del", DelCommand);
            REGISTER_LUA_CACHE_COMMAND("exists", ExistsCommand);
            REGISTER_LUA_CACHE_COMMAND("incr", IncrCommand);
            REGISTER_LUA_CACHE_COMMAND("decr", DecrCommand);
            REGISTER_LUA_CACHE_COMMAND("ttl", TtlCommand);
            REGISTER_LUA_CACHE_COMMAND("mget", MGetCommand);
            REGISTER_LUA_CACHE_COMMAND("mset", MSetCommand);
            REGISTER_LUA_CACHE_COMMAND("keys", KeysCommand);
            // ... 为其他需要在 Lua 中调用的、只需要 cache_ 的命令添加注册行 ...

            // 使用宏注册需要 channel_manager_ 的命令
            // 注意：SUBSCRIBE/UNSUBSCRIBE/PSUBSCRIBE/PUNSUBSCRIBE 通常不在此注册
            REGISTER_LUA_CHANNEL_COMMAND("publish", PublishCommand);
            REGISTER_LUA_CHANNEL_COMMAND("pubsub", PubSubCommand);// <-- 添加这一行来注册 PUBSUB

            // 如果将来添加了新的、需要不同依赖的命令，也需要在这里添加对应的行和宏。
        }
        // --- 新增结束 ---

        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
        std::shared_ptr<apps::ChannelManager> channel_manager_;// 新增：频道管理器
        std::weak_ptr<apps::Session> session_;                 // 新增：存储Session弱指针
        std::shared_ptr<LuaExecutor> lua_executor_;
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
            // 发送命令处理完成事件
            stats::emitCommandProcessed(cmd, argv.size() - 1);// 排除命令名本身

            return command->Execute(argv);
        }

    private:
        CommandFactory factory_;// 工厂包含所有命令的创建逻辑
    };

#undef REGISTER_LUA_CACHE_COMMAND
#undef REGISTER_LUA_CHANNEL_COMMAND

}// namespace Astra::proto