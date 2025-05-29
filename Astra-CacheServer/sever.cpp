#include "args.hxx"
#include "core/astra.hpp"
#include "fmt/color.h"
#include "network/io_context_pool.hpp"

#include "server/server.hpp"
#include "utils/logger.hpp"
#include "utils/util_path.hpp"
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <unordered_map>

using namespace Astra;
using namespace Astra::utils;
using namespace Astra::Persistence;
// Map log level string to LogLevel enum
inline static Astra::LogLevel parseLogLevel(const std::string &levelStr) {
    static const std::unordered_map<std::string, Astra::LogLevel> levels = {
            {"trace", Astra::LogLevel::TRACE},
            {"debug", Astra::LogLevel::DEBUG},
            {"info", Astra::LogLevel::INFO},
            {"warn", Astra::LogLevel::WARN},
            {"error", Astra::LogLevel::ERROR},
            {"fatal", Astra::LogLevel::FATAL}};

    auto it = levels.find(levelStr);
    if (it != levels.end()) {
        return it->second;
    }
    throw std::invalid_argument("Invalid log level: " + levelStr);
}

void writeLogoToConsole(int port, size_t maxLRUSize, const std::string &persistenceFile) {
    // 获取当前进程ID和时间
    pid_t pid = getpid();
    auto timeStr = Astra::Logger::GetInstance().CurrentTimestamp();

    // Redis风格的启动头信息
    fmt::print(fg(fmt::color::light_yellow),
               "{}:C {} * oO0OoO0OoO0Oo Astra-CacheServer is starting oO0OoO0OoO0Oo\n",
               pid, timeStr);

    // 3D Redis风格的艺术字 (红色)
    fmt::print(fg(fmt::color::light_yellow), R"(
                _._                                                  
           _.-``__ ''-._                                             
      _.-``    `.  `_.  ''-._           Astra-CacheServer            
  .-`` .-```.  ```\/    _.,_ ''-._     )");

    // 版本信息(保持青色)
    fmt::print(fg(fmt::color::cyan), "v1.0.0");
    fmt::print(fg(fmt::color::light_yellow), R"( (64 bit)
 (    '      ,       .-`  | `,    )     )");

    // 运行模式(黄色)
    fmt::print(fg(fmt::color::light_yellow), "Standalone mode");
    fmt::print(fg(fmt::color::light_yellow), R"(
 |`-._`-...-` __...-.``-._|'` _.-'|     Port: )");

    // 端口号(青色)
    fmt::print(fg(fmt::color::cyan), "{}", port);
    fmt::print(fg(fmt::color::light_yellow), R"(
 |    `-._   `._    /     _.-'    |     PID: )");

    // PID(青色)
    fmt::print(fg(fmt::color::cyan), "{}", pid);
    fmt::print(fg(fmt::color::light_yellow), R"(
  `-._    `-._  `-./  _.-'    _.-'                                   
 |`-._`-._    `-.__.-'    _.-'_.-'|                                  
 |    `-._`-._        _.-'_.-'    |           )");

    // 项目URL(黄色)
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

    // ASTRA的LOGO (青色)
    fmt::print(fg(fmt::color::cyan), R"(
 █████╗ ███████╗████████╗██████╗  █████╗ 
██╔══██╗██╔════╝╚══██╔══╝██╔══██╗██╔══██╗
███████║███████╗   ██║   ██████╔╝███████║
██╔══██║╚════██║   ██║   ██╔══██╗██╔══██║
██║  ██║███████║   ██║   ██║  ██║██║  ██║
╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝
)");

    // 配置信息 (黄色)
    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Max LRU Size: {}\n", pid, timeStr, maxLRUSize);
    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Persistence File: {}\n", pid, timeStr, persistenceFile);
    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Log Level: {}\n", pid, timeStr, Logger::LevelToString(Astra::Logger::GetInstance().GetLevel()));
    fmt::print(fg(fmt::color::light_yellow),
               "{}:M {} * Initializing server...\n", pid, timeStr);
}

int main(int argc, char *argv[]) {
    // Define command line options
    args::ArgumentParser parser("Astra-Cache Server", "A Redis-compatible Astra Cache server.");
    args::ValueFlag<int> port(parser, "port", "Port number to listen on", {'p', "port"}, 6379);
    args::ValueFlag<std::string> logLevelStr(parser, "level", "Log level: trace, debug, info, warn, error, fatal",
                                             {'l', "loglevel"}, "info");
    args::ValueFlag<std::string> coreDump(parser, "filename", "Core dump file name", {'c', "coredump"},
                                          std::string(getenv("HOME")) + "/.astra/cache_dump.rdb");
    args::ValueFlag<size_t> maxLRUSize(parser, "size", "Maximum size of LRU cache", {'m', "maxsize"}, 100000);

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

    // Get values from command line
    int listeningPort = args::get(port);
    std::string logLevelRaw = args::get(logLevelStr);
    std::string persistence_file_name = args::get(coreDump);
    size_t lru_max_size = args::get(maxLRUSize);

    // Set log level
    try {
        Astra::LogLevel level = parseLogLevel(logLevelRaw);
        ZEN_SET_LEVEL(level);
    } catch (const std::invalid_argument &e) {
        std::cerr << e.what() << std::endl;
        return 2;
    }
    writeLogoToConsole(listeningPort, lru_max_size, persistence_file_name);

    try {
        // 创建 io_service 线程池（线程在这里就启动了）
        auto pool = AsioIOServicePool::GetInstance();
        asio::io_context &io_context = pool->GetIOService();
        asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // 创建服务器实例
        auto server = std::make_shared<Astra::apps::AstraCacheServer>(io_context, lru_max_size, persistence_file_name);

        // 添加信号处理逻辑
        signals.async_wait([server_weak = std::weak_ptr<Astra::apps::AstraCacheServer>(server)](
                                   const boost::system::error_code &, int) {
            if (auto server = server_weak.lock()) {
                ZEN_LOG_INFO("Shutting down server...");
                server->Stop();// 停止服务器资源
            }

            ZEN_LOG_INFO("Stopping IO context pool...");
            std::quick_exit(0);
            //AsioIOServicePool::GetInstance()->Stop();// 容易和线程池冲突，没有释放的必要

            ZEN_LOG_INFO("Graceful shutdown complete.");
        });

        // 启动服务器监听
        server->Start(listeningPort);
        ZEN_LOG_INFO("Astra-CacheServer started on port {}", listeningPort);

        // 主线程也运行 io_context（用于事件循环和信号处理）
        io_context.run();

    } catch (const std::exception &e) {
        ZEN_LOG_ERROR("Error: {}", e.what());
        return 2;
    }

    return 0;
}