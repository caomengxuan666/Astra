#include "core/astra.hpp"
#include "network/io_context_pool.hpp"
#include "server/server.hpp"
#include "utils/logger.hpp"
#include <asio/signal_set.hpp>
#include <memory>

using namespace Astra;
int main(int argc, char *argv[]) {
    try {
        size_t num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;

        // 获取线程池的 io_context
        auto pool = AsioIOServicePool::GetInstance();
        asio::io_context io_context{1};
        asio::signal_set signals(io_context, SIGINT, SIGTERM);

        auto server = std::make_shared<Astra::apps::AstraCacheServer>(io_context, 10000);
        signals.async_wait([&io_context, pool](const boost::system::error_code &, int) {
            ZEN_LOG_INFO("Shutting down server...");
            io_context.stop();
            //pool->Stop();
        });

        server->Start(8080);
        ZEN_LOG_INFO("Astra-CacheServer started on port 8080");

        // 主线程运行 io_context
        io_context.run();


    } catch (const std::exception &e) {
        ZEN_LOG_ERROR("Error: {}", e.what());
        return 2;
    }

    return 0;
}