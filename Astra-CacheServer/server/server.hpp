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
// 添加集群相关头文件
#include "cluster/ClusterCommunicator.hpp"
#include "cluster/ClusterManager.hpp"
#include "config/ConfigManager.h"


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

        void Start(const std::string& bind_address, unsigned short port) {
            asio::ip::tcp::endpoint endpoint(asio::ip::make_address(bind_address), port);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
            ZEN_LOG_INFO("Server listening on {}:{}", bind_address, port);

            LoadCacheFromFile(persistence_db_name_);
            DoAccept();
        }

        //todo 正常实现服务器的退出功能
        void Stop() {
            asio::error_code ec;
            acceptor_.close(ec);

            std::vector<std::shared_ptr<Session>> sessions_to_stop;
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_to_stop = std::move(active_sessions_);
                active_sessions_.clear();
            }

            // 安全地停止所有会话
            for (auto &session: sessions_to_stop) {
                if (session) {
                    session->Stop();
                }
            }

            // 清理会话在ChannelManager中的引用
            if (channel_manager_) {
                channel_manager_.reset();
            }

            task_queue_->Stop();
            SaveToFile(persistence_db_name_);

            // 停止集群通信
            if (cluster_communicator_) {
                cluster_communicator_->Stop();
            }
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

        // 启用集群模式
        void EnableClusterMode(const std::string &local_host, uint16_t cluster_port, uint16_t listening_port) {
            enable_cluster_ = true;

            // 初始化集群管理器
            auto cluster_manager = Astra::cluster::ClusterManager::GetInstance();
            cluster_manager->Initialize(local_host, listening_port);// 使用客户端端口而不是集群端口

            // 更新本地节点的端口信息
            cluster_manager->UpdateNodePorts(
                    cluster_manager->GetLocalNodeId(),
                    listening_port,
                    cluster_port);

            // 初始化集群通信器
            cluster_communicator_ = std::make_unique<Astra::cluster::ClusterCommunicator>(context_);
            cluster_communicator_->Start(static_cast<uint16_t>(cluster_port));

            ZEN_LOG_INFO("Cluster mode enabled on {}:{}", local_host, cluster_port);
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

                            // 如果启用了集群模式，设置集群通信器
                            if (enable_cluster_ && cluster_communicator_) {
                                ZEN_LOG_DEBUG("Setting cluster communicator for new session");
                                session->SetClusterCommunicator(cluster_communicator_.get());
                            } else {
                                if (enable_cluster_) {
                                    ZEN_LOG_WARN("Cluster mode enabled but cluster communicator is not available");
                                }
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
        // 集群相关成员
        bool enable_cluster_ = false;
        std::unique_ptr<Astra::cluster::ClusterCommunicator> cluster_communicator_;
    };

}// namespace Astra::apps