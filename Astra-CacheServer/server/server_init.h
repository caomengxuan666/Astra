//
// Created by Administrator on 25-7-16.
//

#ifndef SERVER_INIT_H
#define SERVER_INIT_H

#include "args.hxx"
#include "core/astra.hpp"
#include "fmt/color.h"
#include "network/io_context_pool.hpp"
#include "persistence/process.hpp"
#include "persistence/util_path.hpp"
#include "server/server.hpp"
#include "utils/logger.hpp"
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <cstdlib>
#include <memory>

// 仅在Windows平台包含插件头文件
#ifdef _WIN32
#include "platform/windows/WindowsServicePlugin.h"
#endif

using namespace Astra;
using namespace Astra::utils;
using namespace Astra::Persistence;

static constexpr size_t MAXLRUSIZE = std::numeric_limits<size_t>::max();

// 用于创建独立进程的函数示例
inline static bool StartTrayProcess(const std::wstring &executablePath) {
    STARTUPINFOW si = {0};// 使用宽字符版本的STARTUPINFO
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(STARTUPINFOW);

    // 创建独立进程，使用宽字符版本CreateProcessW
    BOOL success = CreateProcessW(
            executablePath.c_str(),// 应用程序路径（宽字符）
            NULL,                  // 命令行参数
            NULL,                  // 进程安全属性
            NULL,                  // 线程安全属性
            FALSE,                 // 继承句柄
            0,                     // 创建标志
            NULL,                  // 环境变量
            NULL,                  // 当前目录
            &si,                   // 启动信息（宽字符版本）
            &pi                    // 进程信息
    );

    if (success) {
        // 关闭进程和线程句柄
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }

    return false;
}

// 日志级别解析
inline static Astra::LogLevel parseLogLevel(const std::string &levelStr) {
    static const std::unordered_map<std::string, Astra::LogLevel> levels = {
            {"trace", Astra::LogLevel::TRACE},
            {"debug", Astra::LogLevel::DEBUG},
            {"info", Astra::LogLevel::INFO},
            {"warn", Astra::LogLevel::WARN},
            {"error", Astra::LogLevel::ERR},
            {"fatal", Astra::LogLevel::FATAL}};

    auto it = levels.find(levelStr);
    if (it != levels.end()) {
        return it->second;
    }
    throw std::invalid_argument("Invalid log level: " + levelStr);
}

// 控制台LOGO输出
inline static void writeLogoToConsole(int port, size_t maxLRUSize, const std::string &persistenceFile) noexcept {
    std::string pid = get_pid_str();
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

// 服务器启动核心逻辑（非服务模式）
inline int startServer(int argc, char *argv[]) noexcept {
    // 获取用户主目录
    fs::path homeDir;
    const char *homeEnv = Astra::utils::getEnv();
    if (homeEnv) {
        homeDir = homeEnv;
    } else {
        homeDir = fs::current_path();
        ZEN_LOG_WARN("无法获取用户主目录，使用当前目录: {}", homeDir.string());
    }

    // 构建持久化文件路径
    fs::path dumpFilePath = homeDir / ".astra" / "cache_dump.rdb";

    // 解析命令行参数
    args::ArgumentParser parser("Astra-Cache Server", "A Redis-compatible Astra Cache server.");
    args::ValueFlag<int> port(parser, "port", "Port number to listen on", {'p', "port"}, 6380);
    args::ValueFlag<std::string> logLevelStr(parser, "level", "Log level: trace, debug, info, warn, error, fatal",
                                             {'l', "loglevel"}, "info");
    args::ValueFlag<std::string> coreDump(parser, "filename", "Core dump file name", {'c', "coredump"},
                                          dumpFilePath.string());
    args::ValueFlag<size_t> maxLRUSize(parser, "size", "Maximum size of LRU cache", {'m', "maxsize"}, MAXLRUSIZE);
    args::ValueFlag<bool> enableLoggingFileFlag(parser, "enable", "Enable logging to file", {'f', "file"}, false);

    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Help &) {
        std::cout << parser;
        return 0;
    } catch (const args::ParseError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    // 提取参数值
    int listeningPort = args::get(port);
    std::string logLevelRaw = args::get(logLevelStr);
    std::string persistence_file_name = args::get(coreDump);
    size_t lru_max_size = args::get(maxLRUSize);
    bool enableLoggingFile = args::get(enableLoggingFileFlag);

    // 设置日志级别
    try {
        Astra::LogLevel level = parseLogLevel(logLevelRaw);
        ZEN_SET_LEVEL(level);
    } catch (const std::invalid_argument &e) {
        std::cerr << e.what() << std::endl;
        return 2;
    }

    // 设置日志文件输出
    if (enableLoggingFile) {
        auto &logger = Astra::Logger::GetInstance();
        auto fileAppender = std::make_shared<Astra::SyncFileAppender>(logger.GetDefaultLogDir());
        logger.AddAppender(fileAppender);
    }

    writeLogoToConsole(listeningPort, lru_max_size, persistence_file_name);

    try {
        // 创建IO线程池
        auto pool = AsioIOServicePool::GetInstance();
        asio::io_context &io_context = pool->GetIOService();
        asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // 创建服务器实例
        auto server = std::make_shared<Astra::apps::AstraCacheServer>(io_context, lru_max_size, persistence_file_name);
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

        // 启动服务器
        server->Start(listeningPort);
        ZEN_LOG_INFO("Astra-CacheServer started on port {}", listeningPort);

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
