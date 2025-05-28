#include "args.hxx"
#include "core/astra.hpp"
#include "network/io_context_pool.hpp"
#include "server/server.hpp"
#include "utils/logger.hpp"

#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <memory>
#include <unistd.h>
#include <unordered_map>

using namespace Astra;

// Map log level string to LogLevel enum
Astra::LogLevel parseLogLevel(const std::string &levelStr) {
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

int main(int argc, char *argv[]) {
    // Define command line options
    args::ArgumentParser parser("Astra-Cache Server", "A Redis-compatible Astra Cache server.");
    args::ValueFlag<int> port(parser, "port", "Port number to listen on", {'p', "port"}, 8080);
    args::ValueFlag<std::string> logLevelStr(parser, "level", "Log level: trace, debug, info, warn, error, fatal",
                                             {'l', "loglevel"}, "info");
    args::ValueFlag<std::string> coreDump(parser, "filename", "Core dump file name", {'c', "coredump"},
                                          "cache_dump.rdb");
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

            std::quick_exit(0);// 快速退出，不再等待线程 join
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