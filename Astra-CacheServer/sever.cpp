#include "core/astra.hpp"
#include "network/io_context_pool.hpp"
#include "server/server.hpp"
#include "utils/logger.hpp"
#include <asio/signal_set.hpp>
#include <memory>
#include <unistd.h>

using namespace Astra;

int main(int argc, char *argv[]) {
    try {
        // 创建 io_service 线程池（线程在这里就启动了）
        auto pool = AsioIOServicePool::GetInstance();

        // 获取主线程使用的 io_context（用于信号处理）
        asio::io_context &io_context = pool->GetIOService();
        asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // 创建服务器实例
        auto server = std::make_shared<Astra::apps::AstraCacheServer>(io_context, 10000);

        // 添加信号处理逻辑
        signals.async_wait([server_weak = std::weak_ptr<Astra::apps::AstraCacheServer>(server)](
                                   const boost::system::error_code &, int) {
            if (auto server = server_weak.lock()) {
                ZEN_LOG_INFO("Shutting down server...");
                server->Stop();// 停止服务器资源
            }

            std::quick_exit(0);// ✅ 快速退出，不再等待线程 join
        });

        // 启动服务器监听
        server->Start(8080);
        ZEN_LOG_INFO("Astra-CacheServer started on port 8080");

        // 主线程也运行 io_context（用于事件循环和信号处理）
        io_context.run();

    } catch (const std::exception &e) {
        ZEN_LOG_ERROR("Error: {}", e.what());
        return 2;
    }

    return 0;
}