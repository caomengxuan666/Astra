#include "core/astra.hpp"
#define ZEN_INFO
#include "logger.hpp"
#include "server/server.hpp"
#include <asio/io_context.hpp>
#include <string>

int main(int argc, char *argv[]) {

    try {
        asio::io_context io_context;
        Astra::apps::AstraCacheServer server(io_context);
        int port = 8080;
        server.Start(port);

        ZEN_LOG_INFO("Astra-CacheServer started on port {}", port);

        io_context.run();
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 2;
    }

    return 0;
}