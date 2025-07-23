//
// Created by Administrator on 25-7-16.
//

#ifndef SERVER_INIT_H
#define SERVER_INIT_H
#include "config/ConfigManager.h"
#include "fmt/color.h"
#include "network/io_context_pool.hpp"
#include "persistence/process.hpp"
#include "server.hpp"
#include "server/status_collector.h"
#include "utils/logger.hpp"
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <cstdlib>
#include <memory>
#include <string>


// 仅在Windows平台包含插件头文件
#ifdef _WIN32
#include "platform/windows/WindowsServicePlugin.h"
#endif

using namespace Astra;

static constexpr size_t MAXLRUSIZE = std::numeric_limits<size_t>::max();


// 控制台LOGO输出
inline static void writeLogoToConsole(int port, size_t maxLRUSize, const std::string &persistenceFile) noexcept {
    std::string pid = Astra::persistence::get_pid_str();
    auto timeStr = Astra::Logger::GetInstance().GetTimestamp();

    fmt::print(fg(fmt::color::light_yellow),
               "{}:C {} * oO0OoO0OoO0Oo Astra-CacheServer is starting oO0OoO0OoO0Oo\n",
               pid, timeStr);

    fmt::print(fg(fmt::color::light_yellow), R"(
                _._
           _.-``__ ''-._
      _.-``    `.  `_.  ''-._           Astra-CacheServer
  .-`` .-```.  ```\/    _.,_ ''-._     )");

    fmt::print(fg(fmt::color::cyan), "v1.0.0");
    fmt::print(fg(fmt::color::light_yellow), R"( (64 bit)
 (    '      ,       .-`  | `,    )     )");

    fmt::print(fg(fmt::color::light_yellow), "Standalone mode");
    fmt::print(fg(fmt::color::light_yellow), R"(
 |`-._`-...-` __...-.``-._|'` _.-'|     Port: )");

    fmt::print(fg(fmt::color::cyan), "{}", port);
    fmt::print(fg(fmt::color::light_yellow), R"(
 |    `-._   `._    /     _.-'    |     PID: )");

    fmt::print(fg(fmt::color::cyan), "{}", pid);
    fmt::print(fg(fmt::color::light_yellow), R"(
  `-._    `-._  `-./  _.-'    _.-'
 |`-._`-._    `-.__.-'    _.-'_.-'|
 |    `-._`-._        _.-'_.-'    |           )");

    fmt::print(fg(fmt::color::light_yellow), "https://github.com/caomengxuan666/Astra");
    fmt::print(fg(fmt::color::light_yellow), R"(
  `-._    `-._`-.__.-'_.-'    _.-'
 |`-._`-._    `-.__.-'    _.-'_.-'|
 |    `-._`-._        _.-'_.-'    |
  `-._    `-._`-.__.-'_.-'    _.-'
      `-._    `-.__.-'    _.-'
          `-._        _.-'
              `-.__.-'
)");

    fmt::print(fg(fmt::color::cyan), R"(
 █████╗ ███████╗████████╗██████╗  █████╗
██╔══██╗██╔════╝╚══██╔══╝██╔══██╗██╔══██╗
███████║███████╗   ██║   ██████╔╝███████║
██╔══██║╚════██║   ██║   ██╔══██╗██╔══██║
██║  ██║███████║   ██║   ██║  ██║██║  ██║
╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝
)");

    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Max LRU Size: {}\n", pid, timeStr, maxLRUSize);
    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Persistence File: {}\n", pid, timeStr, persistenceFile);
    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Log Level: {}\n", pid, timeStr, Logger::LevelToString(Astra::Logger::GetInstance().GetLevel()));
    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Initializing server...\n", pid, timeStr);
}

inline static void initServerStatus() {
    auto status_collector = Astra::apps::StatusCollector::GetInstance();
    status_collector->start();
}

// 服务器启动核心逻辑（非服务模式）
inline int startServer(int argc, char *argv[]) noexcept {
    // 初始化配置管理器（单例）
    auto config_manager = apps::ConfigManager::GetInstance();
    if (!config_manager->initialize(argc, argv)) {
        return 1;// 配置解析失败
    }

    // 通过配置管理器获取参数
    uint16_t listening_port = config_manager->getListeningPort();
    size_t max_lru_size = config_manager->getMaxLRUSize();
    std::string persistence_file = config_manager->getPersistenceFileName();

    // 初始化服务器状态收集器
    initServerStatus();

    // 设置日志级别
    try {
        Astra::LogLevel level = config_manager->getLogLevel();
        ZEN_SET_LEVEL(level);
    } catch (const std::invalid_argument &e) {
        std::cerr << e.what() << std::endl;
        return 2;
    }

    // 设置日志文件输出
    if (config_manager->getEnableLoggingFile()) {
        auto &logger = Astra::Logger::GetInstance();
        auto fileAppender = std::make_shared<Astra::SyncFileAppender>(logger.GetDefaultLogDir());
        logger.AddAppender(fileAppender);
    }


    // 打印LOGO
    writeLogoToConsole(listening_port, max_lru_size, persistence_file);

    try {
        // 创建IO线程池
        auto pool = AsioIOServicePool::GetInstance();
        asio::io_context &io_context = pool->GetIOService();
        asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // 创建服务器实例
        auto server = std::make_shared<Astra::apps::AstraCacheServer>(
                io_context, max_lru_size, persistence_file);
        server->setEnablePersistence(false);

        // 信号处理
        signals.async_wait([server_weak = std::weak_ptr<Astra::apps::AstraCacheServer>(server)](
                                   const asio::error_code &, int) {
            if (auto server = server_weak.lock()) {
                ZEN_LOG_INFO("Shutting down server...");
                server->Stop();
            }
            ZEN_LOG_INFO("Stopping IO context pool...");
            std::quick_exit(0);
        });

        // 启动服务器（使用配置的端口）
        server->Start(listening_port);
        ZEN_LOG_INFO("Astra-CacheServer started on port {}", listening_port);

        // 运行IO上下文
        io_context.run();

    } catch (const std::exception &e) {
        ZEN_LOG_ERROR("Error: {}", e.what());
        return 2;
    }

    return 0;
}

// Windows平台服务控制命令处理
#ifdef _WIN32
struct ServiceCommandHandler {
    std::string command;
    bool requiresArgs;
    std::function<bool(WindowsServicePlugin &, int, char **)> handler;
};

inline static const std::vector<ServiceCommandHandler> serviceCommands = {
        {"install", true, [](WindowsServicePlugin &plugin, int argc, char *argv[]) {
             char path[MAX_PATH];
             GetModuleFileNameA(NULL, path, MAX_PATH);
             std::vector<std::string> args;
             for (int i = 2; i < argc; i++) {
                 args.push_back(argv[i]);
             }
             return plugin.install(path, args);
         }},
        {"uninstall", false, [](WindowsServicePlugin &plugin, int, char **) {
             return plugin.uninstall();
         }},
        {"start", false, [](WindowsServicePlugin &plugin, int, char **) {
             return plugin.start();
         }},
        {"stop", false, [](WindowsServicePlugin &plugin, int, char **) {
             return plugin.stop();
         }},
        {"autostart", false, [](WindowsServicePlugin &plugin, int argc, char **argv) {
             bool enable = (argc < 3) || (std::string(argv[2]) != "off");
             return plugin.setAutoStart(enable);
         }}};

// 处理服务控制命令
inline bool handleServiceCommand(int argc, char *argv[]) {
    if (argc < 2) return false;

    std::string command = argv[1];
    for (const auto &cmd: serviceCommands) {
        if (command == cmd.command) {
            WindowsServicePlugin winPlugin;
            bool success = cmd.handler(winPlugin, argc, argv);
            std::cout << (success ? "Service " + command + " successfully" : "Failed to " + command + " service") << std::endl;
            if (command == "start") {
                //StartTrayProcess(L"./TrayPluginExample.exe");
            }
            return success;
        }
    }
    return false;
}
#endif// _WIN32

#endif//SERVER_INIT_H
