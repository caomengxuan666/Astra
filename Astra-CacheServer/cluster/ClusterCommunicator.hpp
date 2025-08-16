#pragma once

#include "ClusterBus.hpp"
#include "ClusterManager.hpp"
#include <asio.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace Astra::cluster {

    // Forward declaration
    struct BusConn;
    namespace bus {
        struct Parsed;
    }

    // 函数前向声明
    void StartReadLoop(std::shared_ptr<BusConn> bc, std::shared_ptr<class ClusterCommunicator> self);
    void HandleFrame(std::shared_ptr<BusConn> bc, const bus::Parsed &p, std::shared_ptr<ClusterCommunicator> self);

    // PING间隔（毫秒）
    constexpr int PING_INTERVAL_MS = 1000;// 1秒

    class ClusterCommunicator : public std::enable_shared_from_this<ClusterCommunicator> {
        // 声明友元函数
        friend void StartReadLoop(std::shared_ptr<BusConn> bc, std::shared_ptr<ClusterCommunicator> self);
        friend void HandleFrame(std::shared_ptr<BusConn> bc, const bus::Parsed &p, std::shared_ptr<ClusterCommunicator> self);

    public:
        explicit ClusterCommunicator(asio::io_context &io_context);
        ~ClusterCommunicator() = default;

        // 启动集群通信器（监听端口）
        void Start(uint16_t cluster_port);

        // 停止集群通信器
        void Stop();

        // 连接到指定节点（使用集群端口）
        bool ConnectToNode(const std::string &node_id, const std::string &host, uint16_t port);

        // 发送PING消息到指定节点
        void SendPing(const std::string &node_id);

        // 发送PING消息到所有节点
        void SendPingToAllNodes();

    protected:
        std::shared_ptr<ClusterManager> cluster_manager_;

    private:
        asio::io_context &io_context_;
        asio::ip::tcp::acceptor acceptor_;
        asio::steady_timer ping_timer_;
        std::unordered_map<std::string, std::shared_ptr<asio::ip::tcp::socket>> node_connections_;
        uint16_t cluster_port_;
        bool is_running_;

        // 启动接受连接
        void StartAccept();

        // 处理接受到的连接
        void HandleAccept(std::shared_ptr<asio::ip::tcp::socket> socket, const asio::error_code &error);

        // 启动PING定时器
        void StartPingTimer();
    };

}// namespace Astra::cluster