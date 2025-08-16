#include "ClusterCommunicator.hpp"
#include "ClusterBus.hpp"
#include "logger.hpp"
#include <asio.hpp>
#include <memory>
#include <random>


// 添加辅助函数用于选择节点标志
static uint16_t nodeFlags(const std::shared_ptr<Astra::cluster::ClusterNode> &n) {
    uint16_t f = 0;
    f |= n->is_master ? Astra::cluster::bus::NODE_MASTER : Astra::cluster::bus::NODE_SLAVE;
    if (n->fail_confirmed) f |= Astra::cluster::bus::NODE_FAIL;
    else if (n->fail_flag)
        f |= Astra::cluster::bus::NODE_PFAIL;
    return f;
}

// 前向声明
namespace Astra::cluster {
    void StartReadLoop(std::shared_ptr<BusConn> bc, std::shared_ptr<ClusterCommunicator> self);
    void HandleFrame(std::shared_ptr<BusConn> bc, const bus::Parsed &p, std::shared_ptr<ClusterCommunicator> self);
}// namespace Astra::cluster

namespace Astra::cluster {

    // 添加 BusConn 结构体用于处理二进制协议连接
    struct BusConn : public std::enable_shared_from_this<BusConn> {
        std::shared_ptr<asio::ip::tcp::socket> sock;
        std::vector<uint8_t> buf;
        explicit BusConn(std::shared_ptr<asio::ip::tcp::socket> s) : sock(std::move(s)) {
            buf.reserve(4096);
        }
    };

    ClusterCommunicator::ClusterCommunicator(asio::io_context &io_context)
        : io_context_(io_context),
          acceptor_(io_context),
          ping_timer_(io_context),// 正确初始化定时器
          cluster_manager_(ClusterManager::GetInstance()),
          cluster_port_(0),
          is_running_(false) {
    }

    void ClusterCommunicator::Start(uint16_t cluster_port) {
        cluster_port_ = cluster_port;

        // 实际启动集群端口监听（Redis规范：使用独立的集群总线端口）
        try {
            asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), cluster_port);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();

            is_running_ = true;
            StartAccept();
            StartPingTimer();// 启动PING定时器
                             // 启动后连接所有已知节点（除了自己）
            auto all_nodes = cluster_manager_->GetAllNodes();
            for (const auto &pair: all_nodes) {
                const auto &node = pair.second;
                if (node->id == cluster_manager_->GetLocalNodeId()) {
                    continue;// 跳过自己
                }

                // 如果尚未连接，则连接
                if (node_connections_.find(node->id) == node_connections_.end()) {
                    ZEN_LOG_INFO("Connecting to known node {} at {}:{}",
                                 node->id, node->host, node->client_port);
                    ConnectToNode(node->id, node->host, node->client_port);
                }
            }

            ZEN_LOG_INFO("Cluster communicator listening on port {}", cluster_port);
        } catch (const std::exception &e) {
            ZEN_LOG_ERROR("Failed to start cluster communicator on port {}: {}", cluster_port, e.what());
        }
    }

    void ClusterCommunicator::Stop() {
        is_running_ = false;
        asio::error_code ec;
        acceptor_.close(ec);

        // 关闭所有节点连接
        for (auto &pair: node_connections_) {
            if (pair.second->is_open()) {
                pair.second->close(ec);
            }
        }
        node_connections_.clear();

        ZEN_LOG_INFO("Cluster communicator stopped");
    }

    bool ClusterCommunicator::ConnectToNode(const std::string &node_id, const std::string &host, uint16_t port) {
        // 实际建立到目标节点集群端口的连接（Redis规范）
        auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);

        try {
            // 获取目标节点的集群端口
            uint16_t cluster_port = port + 10000;// 默认偏移量

            // 如果已知节点信息，使用其集群端口
            if (!node_id.empty()) {
                if (auto node = cluster_manager_->GetNode(node_id)) {
                    cluster_port = node->cluster_port;
                    ZEN_LOG_DEBUG("Using cluster port {} for node {}", cluster_port, node_id);
                }
            }

            ZEN_LOG_INFO("Attempting to connect to node at {}:{} (cluster bus port: {})", host, port, cluster_port);

            // Redis集群规范：使用集群总线端口
            asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(host), cluster_port);
            socket->async_connect(endpoint,
                                  [this, node_id, socket, host, port, cluster_port](const asio::error_code &ec) {
                                      if (!ec) {
                                          ZEN_LOG_INFO("Connected to node at {}:{} (cluster bus)", host, cluster_port);
                                          // 即使node_id为空，我们也建立连接
                                          if (!node_id.empty()) {
                                              node_connections_[node_id] = socket;
                                              ZEN_LOG_DEBUG("Added connection for node {} to connection map", node_id);
                                          }

                                          // 发送 MEET 帧完成握手，使用二进制协议而不是RESP
                                          auto me = cluster_manager_->GetNode(cluster_manager_->GetLocalNodeId());
                                          if (!me) {
                                              ZEN_LOG_ERROR("Failed to get local node info");
                                              return;
                                          }

                                          ZEN_LOG_DEBUG("Preparing to send MEET frame from node {} to {}", me->id, node_id);

                                          // 准备gossip信息
                                          std::vector<Astra::cluster::bus::GossipEntry> gossip;
                                          auto all_nodes = cluster_manager_->GetAllNodes();
                                          ZEN_LOG_DEBUG("Preparing gossip with {} known nodes", all_nodes.size());

                                          for (const auto &pair: all_nodes) {
                                              const auto &node = pair.second;
                                              if (node->id == cluster_manager_->GetLocalNodeId() || node->id == node_id)
                                                  continue;

                                              ZEN_LOG_DEBUG("Adding gossip node: {} at {}:{}", node->id, node->host, node->client_port);
                                              gossip.push_back(Astra::cluster::bus::makeGossip(
                                                      node->id,
                                                      node->host,
                                                      node->client_port,
                                                      node->cluster_port,
                                                      nodeFlags(node)));
                                          }

                                          // 随机选择部分gossip节点（最多3个）
                                          if (gossip.size() > 3) {
                                              std::random_device rd;
                                              std::mt19937 g(rd());
                                              std::shuffle(gossip.begin(), gossip.end(), g);
                                              gossip.resize(3);
                                          }

                                          ZEN_LOG_DEBUG("Sending MEET with {} gossip entries", gossip.size());

                                          auto frame = Astra::cluster::bus::buildFrame(
                                                  Astra::cluster::bus::MsgType::MEET,
                                                  me->id,
                                                  me->host,
                                                  me->client_port,
                                                  me->cluster_port,
                                                  nodeFlags(me),
                                                  cluster_manager_->GetCurrentEpoch(),
                                                  me->config_epoch,
                                                  gossip);
                                          auto out = std::make_shared<std::vector<uint8_t>>(std::move(frame));
                                          asio::async_write(*socket, asio::buffer(*out),
                                                            [this, node_id, socket, out, host, cluster_port](const asio::error_code &write_ec, size_t) {
                                                                if (!write_ec) {
                                                                    ZEN_LOG_INFO("Sent MEET frame to {}:{} (cluster bus)", host, cluster_port);

                                                                    // 启动读取循环
                                                                    auto bc = std::make_shared<BusConn>(socket);
                                                                    StartReadLoop(bc, shared_from_this());
                                                                } else {
                                                                    ZEN_LOG_ERROR("Failed to send MEET frame to {}:{} (cluster bus): {}", host, cluster_port, write_ec.message());
                                                                }
                                                            });
                                      } else {
                                          ZEN_LOG_ERROR("Failed to connect to node at {}:{} (cluster bus): {}", host, cluster_port, ec.message());
                                      }
                                  });

            return true;
        } catch (const std::exception &e) {
            ZEN_LOG_ERROR("Exception while connecting to node at {}:{} (cluster bus): {}", host, port + 10000, e.what());
            return false;
        }
    }

    void ClusterCommunicator::StartAccept() {
        if (!is_running_) return;

        auto new_socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
        acceptor_.async_accept(*new_socket,
                               [this, new_socket](const asio::error_code &error) {
                                   HandleAccept(new_socket, error);
                               });
    }

    void ClusterCommunicator::HandleAccept(std::shared_ptr<asio::ip::tcp::socket> socket,
                                           const asio::error_code &error) {
        if (!error && is_running_) {
            ZEN_LOG_INFO("New cluster connection accepted from {}",
                         socket->remote_endpoint().address().to_string());

            // 为新连接启动读取操作，使用二进制协议解析
            auto bc = std::make_shared<BusConn>(socket);
            StartReadLoop(bc, shared_from_this());

            // 继续接受新连接
            StartAccept();
        } else if (error != asio::error::operation_aborted) {
            ZEN_LOG_WARN("Error accepting cluster connection: {}", error.message());
        }
    }

    void StartReadLoop(std::shared_ptr<BusConn> bc, std::shared_ptr<ClusterCommunicator> self) {
        // 创建一个可复制的读取头部函数对象
        struct ReadHeaderHelper {
            static void readHeader(std::shared_ptr<BusConn> bc, std::shared_ptr<ClusterCommunicator> self) {
                bc->buf.resize(sizeof(Astra::cluster::bus::BusHeader));
                asio::async_read(*bc->sock, asio::buffer(bc->buf.data(), bc->buf.size()),
                                 [bc, self](const asio::error_code &ec, size_t) {
                                     if (ec) {
                                         auto remote_endpoint = bc->sock->remote_endpoint();
                                         ZEN_LOG_WARN("bus read header error from {}:{}: {}",
                                                      remote_endpoint.address().to_string(), remote_endpoint.port(), ec.message());
                                         return;
                                     }

                                     Astra::cluster::bus::BusHeader h{};
                                     std::memcpy(&h, bc->buf.data(), sizeof(h));

                                     if (std::memcmp(h.signature, "RCmb", 4) != 0) {
                                         ZEN_LOG_WARN("bad bus signature: {:.4s} expected RCmb", h.signature);
                                         return;
                                     }

                                     const uint32_t tot = Astra::cluster::bus::n2h32(h.totlen);
                                     if (tot < sizeof(Astra::cluster::bus::BusHeader) || tot > (1 << 20)) {
                                         ZEN_LOG_WARN("bad totlen {}", tot);
                                         return;
                                     }

                                     const size_t remain = tot - sizeof(Astra::cluster::bus::BusHeader);
                                     if (remain == 0) {
                                         // 解析帧
                                         try {
                                             auto parsed = Astra::cluster::bus::parseFrame(bc->buf.data(), bc->buf.size());
                                             HandleFrame(bc, parsed, self);
                                         } catch (const std::exception &e) {
                                             ZEN_LOG_WARN("parse err: {}", e.what());
                                         }

                                         // 继续读取下一个帧
                                         readHeader(bc, self);
                                         return;
                                     }

                                     // 读取剩余数据
                                     size_t off = bc->buf.size();
                                     bc->buf.resize(tot);
                                     asio::async_read(*bc->sock, asio::buffer(bc->buf.data() + off, remain),
                                                      [bc, self](const asio::error_code &bec, size_t) {
                                                          if (bec) {
                                                              ZEN_LOG_WARN("bus read body error: {}", bec.message());
                                                              return;
                                                          }

                                                          try {
                                                              auto parsed = Astra::cluster::bus::parseFrame(bc->buf.data(), bc->buf.size());
                                                              HandleFrame(bc, parsed, self);
                                                          } catch (const std::exception &e) {
                                                              ZEN_LOG_WARN("parse err: {}", e.what());
                                                          }

                                                          // 继续读取下一个帧
                                                          readHeader(bc, self);
                                                      });
                                 });
            }
        };

        ReadHeaderHelper::readHeader(bc, self);
    }

    void HandleFrame(std::shared_ptr<BusConn> bc, const Astra::cluster::bus::Parsed &p, std::shared_ptr<ClusterCommunicator> self) {
        ZEN_LOG_DEBUG("Handling frame type {} from node {} at {}:{}",
                      static_cast<int>(p.type), p.senderId, p.myip, p.port);

        // 根据收到的帧信息更新或添加节点
        auto n = self->cluster_manager_->GetNode(p.senderId);
        if (!n) {
            ZEN_LOG_INFO("Adding new node {} at {}:{}", p.senderId, p.myip, p.port);
            self->cluster_manager_->AddNode(p.senderId, p.myip, p.port, p.cport, (p.flags & Astra::cluster::bus::NODE_MASTER) != 0);
            n = self->cluster_manager_->GetNode(p.senderId);
        } else {
            ZEN_LOG_DEBUG("Updating existing node {} with ports {}:{}", p.senderId, p.port, p.cport);
            self->cluster_manager_->UpdateNodePorts(p.senderId, p.port, p.cport);
            n->host = p.myip;
            n->is_master = (p.flags & Astra::cluster::bus::NODE_MASTER) != 0;
            n->fail_flag = (p.flags & Astra::cluster::bus::NODE_PFAIL) != 0;
            n->fail_confirmed = (p.flags & Astra::cluster::bus::NODE_FAIL) != 0;
            n->config_epoch = p.configEpoch;
        }

        // 处理gossip条目
        ZEN_LOG_DEBUG("Processing {} gossip entries", p.gossip.size());
        for (const auto &entry: p.gossip) {
            std::string nodeId(entry.nodeId, strnlen(entry.nodeId, Astra::cluster::bus::NODE_ID_LEN));
            std::string ip(entry.ip, strnlen(entry.ip, Astra::cluster::bus::IP_STR_LEN));

            // 只处理非本地节点
            if (nodeId == self->cluster_manager_->GetLocalNodeId()) {
                ZEN_LOG_DEBUG("Skipping gossip entry for local node");
                continue;
            }

            ZEN_LOG_DEBUG("Processing gossip node: {}", nodeId);

            // 获取或创建节点
            auto node = self->cluster_manager_->GetNode(nodeId);
            if (!node) {
                uint16_t clientPort = Astra::cluster::bus::n2h16(entry.port);
                uint16_t clusterPort = Astra::cluster::bus::n2h16(entry.cport);
                bool isMaster = (entry.flags & Astra::cluster::bus::NODE_MASTER) != 0;

                self->cluster_manager_->AddNode(
                        nodeId,
                        ip,
                        clientPort,
                        clusterPort,
                        isMaster);
                node = self->cluster_manager_->GetNode(nodeId);
                ZEN_LOG_INFO("Added gossip node: {} at {}:{}@{}", nodeId, ip, clientPort, clusterPort);
            }

            // 更新节点状态
            if (node) {
                node->host = ip;
                node->client_port = Astra::cluster::bus::n2h16(entry.port);
                node->cluster_port = Astra::cluster::bus::n2h16(entry.cport);
                node->is_master = (entry.flags & Astra::cluster::bus::NODE_MASTER) != 0;
                node->fail_flag = (entry.flags & Astra::cluster::bus::NODE_PFAIL) != 0;
                node->fail_confirmed = (entry.flags & Astra::cluster::bus::NODE_FAIL) != 0;
                ZEN_LOG_DEBUG("Updated gossip node: {} at {}:{}@{}", nodeId, ip, node->client_port, node->cluster_port);
            }
        }

        switch (p.type) {
            case Astra::cluster::bus::MsgType::MEET:
                ZEN_LOG_INFO("Received MEET from node {}, sending PONG", p.senderId);
                [[fallthrough]];
            case Astra::cluster::bus::MsgType::PING: {
                ZEN_LOG_DEBUG("Received PING/MEET from node {}, sending PONG", p.senderId);
                // 回复 PONG
                auto me = self->cluster_manager_->GetNode(self->cluster_manager_->GetLocalNodeId());

                // 获取要发送的gossip（排除目标节点和本地节点）
                std::vector<Astra::cluster::bus::GossipEntry> gossip;
                auto all_nodes = self->cluster_manager_->GetAllNodes();
                for (const auto &pair: all_nodes) {
                    const auto &node = pair.second;
                    if (node->id == self->cluster_manager_->GetLocalNodeId() || node->id == p.senderId)
                        continue;

                    gossip.push_back(Astra::cluster::bus::makeGossip(
                            node->id,
                            node->host,
                            node->client_port,
                            node->cluster_port,
                            nodeFlags(node)));
                }

                // 随机选择部分gossip节点（最多3个）
                if (gossip.size() > 3) {
                    std::random_device rd;
                    std::mt19937 g(rd());
                    std::shuffle(gossip.begin(), gossip.end(), g);
                    gossip.resize(3);
                }

                auto frame = Astra::cluster::bus::buildFrame(
                        Astra::cluster::bus::MsgType::PONG,
                        me->id,
                        me->host,
                        me->client_port,
                        me->cluster_port,
                        nodeFlags(me),
                        self->cluster_manager_->GetCurrentEpoch(),
                        me->config_epoch,
                        gossip);
                auto out = std::make_shared<std::vector<uint8_t>>(std::move(frame));
                asio::async_write(*bc->sock, asio::buffer(*out),
                                  [out, p](const asio::error_code &wec, size_t) {
                                      if (wec) {
                                          ZEN_LOG_WARN("Failed to send PONG to node {}: {}", p.senderId, wec.message());
                                      } else {
                                          ZEN_LOG_DEBUG("Sent PONG to node {}", p.senderId);
                                      }
                                      // 忽略写入结果
                                      (void) wec;
                                  });
                break;
            }
            case Astra::cluster::bus::MsgType::PONG: {
                ZEN_LOG_DEBUG("Received PONG from node {}", p.senderId);
                self->cluster_manager_->UpdateNodePongTime(p.senderId);
                // 同时更新最后ping时间（因为PONG是对PING的响应）
                self->cluster_manager_->UpdateNodePingTime(p.senderId);
                break;
            }
            default:
                ZEN_LOG_WARN("Unknown message type received: {}", static_cast<int>(p.type));
                break;
        }
    }

    void ClusterCommunicator::StartPingTimer() {
        // 启动定期PING定时器
        ping_timer_.expires_after(std::chrono::milliseconds(PING_INTERVAL_MS));
        ping_timer_.async_wait([this](const asio::error_code &ec) {
            if (!ec && is_running_) {
                SendPingToAllNodes();
                StartPingTimer();// 重新启动定时器
            }
        });
    }

    void ClusterCommunicator::SendPingToAllNodes() {
        // 发送PING消息到所有已连接的节点
        ZEN_LOG_DEBUG("Sending PING to all {} connected nodes", node_connections_.size());
        for (const auto &pair: node_connections_) {
            const std::string &node_id = pair.first;
            const auto &socket = pair.second;

            if (socket->is_open()) {
                SendPing(node_id);
            } else {
                ZEN_LOG_DEBUG("Socket for node {} is not open", node_id);
            }
        }
    }

    void ClusterCommunicator::SendPing(const std::string &node_id) {
        // 检查节点是否存在和连接是否打开
        auto it = node_connections_.find(node_id);
        if (it == node_connections_.end() || !it->second->is_open()) {
            ZEN_LOG_DEBUG("Cannot send PING to node {}: node not connected", node_id);
            return;
        }

        auto socket = it->second;

        // 构建PING帧，使用二进制协议
        auto me = cluster_manager_->GetNode(cluster_manager_->GetLocalNodeId());
        std::vector<Astra::cluster::bus::GossipEntry> gos;// TODO: 添加一些gossip节点
        auto frame = Astra::cluster::bus::buildFrame(
                Astra::cluster::bus::MsgType::PING,
                me->id,
                me->host,
                me->client_port,
                me->cluster_port,
                nodeFlags(me),
                cluster_manager_->GetCurrentEpoch(),
                me->config_epoch,
                gos);
        auto out = std::make_shared<std::vector<uint8_t>>(std::move(frame));

        // 发送PING帧
        asio::async_write(*socket, asio::buffer(*out),
                          [this, node_id, out](const asio::error_code &ec, size_t) {
                              if (!ec) {
                                  ZEN_LOG_DEBUG("Sent PING to node {}", node_id);
                              } else {
                                  ZEN_LOG_ERROR("Failed to send PING to node {}: {}", node_id, ec.message());
                              }
                          });
    }
}// namespace Astra::cluster