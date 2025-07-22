#pragma once

#include "logger.hpp"
#include "persistence/persistence.hpp"
#include "session.hpp"
#include <asio.hpp>
#include <asio/io_context.hpp>
#include <concurrent/task_queue.hpp>
#include <datastructures/lockfree_queue.hpp>
#include <datastructures/lru_cache.hpp>
#include <fmt/format.h>
#include <memory>


namespace Astra::apps {
    using namespace Astra::proto;
    class AstraCacheServer {
    public:
        explicit AstraCacheServer(asio::io_context &context, size_t cache_size,
                                  const std::string &persistent_file)
            : context_(context),
              cache_(std::make_shared<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>>(cache_size)),
              acceptor_(context),
              persistence_db_name_(persistent_file),
              channel_manager_(ChannelManager::GetInstance()) {
            const int thread_count = std::thread::hardware_concurrency() / 2;
            task_queue_ = std::make_shared<concurrent::TaskQueue>(thread_count);
        }

        void Start(unsigned short port) {
            asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
            ZEN_LOG_INFO("Server listening on port {}", port);

            LoadCacheFromFile(persistence_db_name_);
            DoAccept();
        }

        void Stop() {
            asio::error_code ec;
            acceptor_.close(ec);

            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto &session: active_sessions_) {
                session->Stop();
            }
            active_sessions_.clear();

            task_queue_->Stop();
            SaveToFile(persistence_db_name_);
        }

        void SaveToFile(const std::string &filename) {
            if (!enable_persistence_) return;
            Astra::Persistence::SaveCacheToFile(*cache_.get(), filename);
        }

        void LoadCacheFromFile(const std::string &filename) {
            if (!enable_persistence_) return;
            ZEN_LOG_INFO("Loading cache from {}", persistence_db_name_);
            Astra::Persistence::LoadCacheFromFile(*cache_.get(), filename);
        }

        void setEnablePersistence(bool enable) {
            enable_persistence_ = enable;
        }

    private:
        void DoAccept() {
            acceptor_.async_accept(
                    asio::make_strand(context_),
                    [this](std::error_code ec, asio::ip::tcp::socket socket) {
                        if (!ec) {
                            ZEN_LOG_INFO("New client accepted from: {}",
                                         socket.remote_endpoint().address().to_string());

                            auto session = std::make_shared<Session>(
                                    std::move(socket), cache_, task_queue_, channel_manager_);
                            {
                                std::lock_guard<std::mutex> lock(sessions_mutex_);
                                active_sessions_.push_back(session);
                            }

                            session->Start();
                        } else if (ec != asio::error::operation_aborted) {
                            ZEN_LOG_WARN("Accept error: {}", ec.message());
                        }

                        if (!acceptor_.is_open()) {
                            ZEN_LOG_INFO("Acceptor closed, stopping accept loop");
                            return;
                        }
                        DoAccept();
                    });
        }

        std::string persistence_db_name_;
        asio::io_context &context_;
        asio::ip::tcp::acceptor acceptor_;
        std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache_;
        std::shared_ptr<concurrent::TaskQueue> task_queue_;
        std::vector<std::shared_ptr<Session>> active_sessions_;
        std::mutex sessions_mutex_;
        bool enable_persistence_ = false;
        std::shared_ptr<ChannelManager> channel_manager_;// 持有ChannelManager实例
    };

}// namespace Astra::apps
