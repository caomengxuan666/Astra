#include "ClusterSession.hpp"
#include "ClusterCommunicator.hpp"
#include "ClusterManager.hpp"
#include "proto/resp_builder.hpp"
#include "utils/CRC16.hpp"
#include "utils/logger.hpp"
#include <asio.hpp>
#include <fmt/format.h>


namespace Astra::cluster {

    ClusterSession::ClusterSession(
            std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache)
        : cache_(cache), cluster_manager_(ClusterManager::GetInstance()), cluster_communicator_(nullptr) {
    }

    std::string ClusterSession::ProcessClientRequest(const std::string &command, const std::vector<std::string> &args) {
        // 检查是否是集群相关命令
        if (command == "CLUSTER") {
            if (args.empty()) {
                return Astra::proto::RespBuilder::Error("Wrong number of arguments for 'CLUSTER' command");
            }

            const std::string subcommand = args[0];
            if (subcommand == "NODES") {
                // 返回集群节点信息
                std::string result;
                for (const auto &pair: cluster_manager_->GetAllNodes()) {
                    const auto &node = pair.second;

                    // 节点ID
                    result += node->id;
                    result += " ";

                    // 地址信息 host:client_port@cluster_port
                    result += node->host + ":" + std::to_string(node->client_port) + "@" + std::to_string(node->cluster_port);
                    result += " ";

                    // 标志
                    std::string flags = "";
                    if (node->id == cluster_manager_->GetLocalNodeId()) {
                        flags += "myself,";
                    }
                    flags += node->is_master ? "master" : "slave";

                    if (node->fail_confirmed) {
                        flags += ",fail";
                    } else if (node->fail_flag) {
                        flags += ",fail?";
                    }

                    result += flags;
                    result += " ";

                    // 主节点ID（从节点使用）
                    result += "-";
                    result += " ";

                    // ping发送时间
                    result += std::to_string(node->last_ping_time);
                    result += " ";

                    // pong接收时间
                    result += std::to_string(node->last_pong_time);
                    result += " ";

                    // epoch
                    result += std::to_string(node->config_epoch);
                    result += " ";

                    // 连接状态
                    result += "connected";
                    result += " ";

                    // 槽位范围
                    auto slot_ranges = cluster_manager_->GetSlotRangesForNode(node->id);
                    for (size_t i = 0; i < slot_ranges.size(); ++i) {
                        const auto &range = slot_ranges[i];
                        result += std::to_string(range.first);
                        if (range.first != range.second) {
                            result += "-" + std::to_string(range.second);
                        }
                        if (i < slot_ranges.size() - 1) {
                            result += " ";
                        }
                    }

                    result += "\n";
                }
                return Astra::proto::RespBuilder::BulkString(result);
            } else if (subcommand == "SLOTS") {
                // 构建槽位信息
                std::vector<std::string> slot_info;

                // 获取所有节点信息
                const auto &all_nodes = cluster_manager_->GetAllNodes();
                for (const auto &pair: all_nodes) {
                    const auto &node = pair.second;

                    // 为每个槽位范围创建条目
                    auto slot_ranges = cluster_manager_->GetSlotRangesForNode(node->id);
                    for (const auto &range: slot_ranges) {
                        std::vector<std::string> master_info;
                        master_info.push_back(Astra::proto::RespBuilder::BulkString(node->host));
                        master_info.push_back(Astra::proto::RespBuilder::Integer(node->client_port));// 使用客户端端口
                        master_info.push_back(Astra::proto::RespBuilder::BulkString(node->id));
                        std::string master_array = Astra::proto::RespBuilder::Array(master_info);

                        std::vector<std::string> slot_range;
                        slot_range.push_back(Astra::proto::RespBuilder::Integer(range.first));
                        slot_range.push_back(Astra::proto::RespBuilder::Integer(range.second));
                        slot_range.push_back(master_array);

                        slot_info.push_back(Astra::proto::RespBuilder::Array(slot_range));
                    }
                }

                return Astra::proto::RespBuilder::Array(slot_info);
            } else if (subcommand == "ADDSLOTS") {
                // 添加槽位到当前节点
                if (args.size() < 2) {
                    return Astra::proto::RespBuilder::Error("Wrong number of arguments for 'CLUSTER ADDSLOTS' command");
                }

                // 解析槽位范围
                for (size_t i = 1; i < args.size(); ++i) {
                    std::string slot_arg = args[i];
                    size_t dash_pos = slot_arg.find('-');

                    if (dash_pos != std::string::npos) {
                        // 槽位范围
                        try {
                            uint16_t start_slot = static_cast<uint16_t>(std::stoi(slot_arg.substr(0, dash_pos)));
                            uint16_t end_slot = static_cast<uint16_t>(std::stoi(slot_arg.substr(dash_pos + 1)));

                            if (start_slot <= end_slot && end_slot < SLOT_COUNT) {
                                cluster_manager_->AddSlotRange(start_slot, end_slot, cluster_manager_->GetLocalNodeId());
                            } else {
                                return Astra::proto::RespBuilder::Error("Invalid slot range");
                            }
                        } catch (const std::exception &) {
                            return Astra::proto::RespBuilder::Error("Invalid slot range format");
                        }
                    } else {
                        // 单个槽位
                        try {
                            uint16_t slot = static_cast<uint16_t>(std::stoi(slot_arg));
                            if (slot < SLOT_COUNT) {
                                cluster_manager_->AddSlotRange(slot, slot, cluster_manager_->GetLocalNodeId());
                            } else {
                                return Astra::proto::RespBuilder::Error("Invalid slot number");
                            }
                        } catch (const std::exception &) {
                            return Astra::proto::RespBuilder::Error("Invalid slot number");
                        }
                    }
                }

                // 保存配置
                cluster_manager_->SaveNodesConfig("nodes.conf");

                return Astra::proto::RespBuilder::SimpleString("OK");
            } else if (subcommand == "REPLICATE") {
                if (args.size() < 2) {
                    return Astra::proto::RespBuilder::Error("Wrong number of arguments for 'CLUSTER REPLICATE' command");
                }

                std::string master_id = args[1];
                auto master_node = cluster_manager_->GetNode(master_id);
                if (!master_node) {
                    return Astra::proto::RespBuilder::Error("ERR No such master with that ID");
                }

                // 将当前节点设置为slave
                auto local_node = cluster_manager_->GetNode(cluster_manager_->GetLocalNodeId());
                if (local_node) {
                    local_node->is_master = false;
                    // 在实际实现中，还需要设置主节点ID等信息
                    // 这里简化处理
                }

                return Astra::proto::RespBuilder::SimpleString("OK");
            } else if (subcommand == "INFO") {
                // 返回集群信息
                std::string result = "cluster_state:ok\r\n";
                result += "cluster_slots_assigned:0\r\n";// 需要实际计算
                result += "cluster_slots_ok:0\r\n";      // 需要实际计算
                result += "cluster_slots_pfail:0\r\n";
                result += "cluster_slots_fail:0\r\n";
                result += "cluster_known_nodes:" + std::to_string(cluster_manager_->GetAllNodes().size()) + "\r\n";
                result += "cluster_size:0\r\n";// 需要实际计算
                result += "cluster_current_epoch:" + std::to_string(cluster_manager_->GetCurrentEpoch()) + "\r\n";
                result += "cluster_my_epoch:" + std::to_string(cluster_manager_->GetNode(cluster_manager_->GetLocalNodeId())->config_epoch) + "\r\n";

                return Astra::proto::RespBuilder::BulkString(result);
            } else if (subcommand == "MEET") {
                // 添加新节点到集群
                if (args.size() < 3) {
                    return Astra::proto::RespBuilder::Error("Wrong number of arguments for 'CLUSTER MEET' command");
                }

                std::string host = args[1];
                uint16_t port = static_cast<uint16_t>(std::stoi(args[2]));

                ZEN_LOG_INFO("Processing CLUSTER MEET command for {}:{} (client port)", host, port);

                // 修复：不要生成虚假的节点ID，而是在建立连接后从对端获取真实ID
                // 通过集群通信器连接到新节点，真实节点ID将在握手过程中获取
                if (cluster_communicator_) {
                    // 使用空的节点ID占位符，实际ID将在握手后确定
                    // 这里我们不需要node_id，连接建立后通过MEET/PONG消息交换节点信息
                    cluster_communicator_->ConnectToNode("", host, port);
                    ZEN_LOG_INFO("Initiated connection to {}:{} for CLUSTER MEET", host, port);
                } else {
                    ZEN_LOG_WARN("Cluster communicator is not available");
                    // 尝试通过ClusterManager获取集群通信器
                    ZEN_LOG_ERROR("Cluster communicator is not set in ClusterSession");
                }
                return Astra::proto::RespBuilder::SimpleString("OK");
            } else {
                return Astra::proto::RespBuilder::Error("Unknown subcommand or wrong number of arguments for 'CLUSTER'");
            }
        }

        // 处理集群内部MEET消息
        /* 
         * 删除RESP格式的MEET处理，集群总线使用二进制协议，不需要处理 RESP 格式的 MEET
         *
        if (command == "MEET") {
            if (args.size() < 3) {
                return Astra::proto::RespBuilder::Error("ERR Wrong number of arguments for MEET command");
            }

            std::string node_id = args[0];
            std::string host = args[1];
            uint16_t port = static_cast<uint16_t>(std::stoi(args[2]));

            if (!cluster_manager_->GetNode(node_id)) {
                cluster_manager_->AddNode(node_id, host, port, true);
                ZEN_LOG_INFO("Added new node {} from MEET command", node_id);
            }

            // 回复PONG
            std::vector<std::string> pong_items;
            pong_items.push_back("PONG");
            pong_items.push_back(cluster_manager_->GetLocalNodeId());
            pong_items.push_back(std::to_string(cluster_manager_->GetCurrentEpoch()));

            //gossip
            pong_items.push_back(cluster_manager_->GetGossipInfo());
            return Astra::proto::RespBuilder::Array(pong_items);
        }
        */

        // 处理集群内部PING消息
        if (command == "PING" && args.size() >= 3) {
            // 解析 PING 消息
            std::string sender_id = args[0];             // 发送方的 node_id
            uint64_t sender_epoch = std::stoull(args[1]);// 发送方的 config_epoch
            std::string gossip_data = args[2];           // args[2] 是 gossip 数组
            // 1. 更新发送方节点的 last_pong_time
            auto sender_node = cluster_manager_->GetNode(sender_id);
            if (sender_node) {
                sender_node->last_pong_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::system_clock::now().time_since_epoch())
                                                      .count();
            }

            // 2. 处理 Gossip 信息
            ProcessGossip(args[2]);// 处理 gossip 信息

            // 3. 回复 PONG
            std::vector<std::string> pong_msg;
            pong_msg.push_back("PONG");
            pong_msg.push_back(cluster_manager_->GetLocalNodeId());
            pong_msg.push_back(std::to_string(cluster_manager_->GetCurrentEpoch()));
            // 添加 gossip 信息
            pong_msg.push_back(cluster_manager_->GetGossipInfo());
            return Astra::proto::RespBuilder::Array(pong_msg);
        }

        if (command == "PONG") {
            // 确保有足够的参数
            if (args.size() < 4) {
                return Astra::proto::RespBuilder::Error("Invalid PONG message format");
            }

            std::string sender_id = args[0];
            uint64_t sender_epoch = std::stoull(args[1]);
            std::string gossip_data = args[2];// 第 3 个参数是 gossip 数据

            // 更新发送方节点的 last_pong_time
            auto sender_node = cluster_manager_->GetNode(sender_id);
            if (sender_node) {
                cluster_manager_->UpdateNodePongTime(sender_id);
            }

            // 处理 Gossip 信息
            ProcessGossip(gossip_data);

            // 不需要回复，返回空响应
            return Astra::proto::RespBuilder::SimpleString("OK");
        }

        // 对于普通命令，检查键是否在当前节点
        if (!args.empty()) {
            std::string key = args[0];
            if (!IsKeyLocal(key)) {
                // 键不在当前节点，需要重定向
                return GenerateRedirectResponse(key);
            }
        }

        // 键在当前节点，正常处理命令
        // 这里应该调用实际的命令处理逻辑，暂时返回一个默认响应
        if (command == "GET") {
            if (args.empty()) {
                return Astra::proto::RespBuilder::Error("wrong number of arguments for 'GET' command");
            }

            auto result = cache_->Get(args[0]);
            if (result.has_value()) {
                const std::string &value = result.value();
                return Astra::proto::RespBuilder::BulkString(value);
            } else {
                return Astra::proto::RespBuilder::Nil();
            }
        } else if (command == "SET") {
            if (args.size() < 2) {
                return Astra::proto::RespBuilder::Error("wrong number of arguments for 'SET' command");
            }

            cache_->Put(args[0], args[1]);
            return Astra::proto::RespBuilder::SimpleString("OK");
        } else if (command == "DEL") {
            if (args.empty()) {
                return Astra::proto::RespBuilder::Error("wrong number of arguments for 'DEL' command");
            }

            size_t deleted = 0;
            for (const auto &key: args) {
                if (cache_->Remove(key)) {
                    deleted++;
                }
            }
            return Astra::proto::RespBuilder::Integer(deleted);
        } else {
            // 其他命令暂时返回未实现
            return Astra::proto::RespBuilder::Error("command '" + command + "' not supported in cluster mode");
        }
    }

    bool ClusterSession::IsKeyLocal(const std::string &key) const {
        std::string target_node_id = cluster_manager_->GetNodeForKey(key);
        // 如果槽位未分配，允许在任何节点上处理
        if (target_node_id.empty()) {
            return true;
        }
        return target_node_id == cluster_manager_->GetLocalNodeId();
    }

    std::string ClusterSession::GenerateRedirectResponse(const std::string &key) const {
        std::string target_node_id = cluster_manager_->GetNodeForKey(key);
        auto node = cluster_manager_->GetNode(target_node_id);

        if (!node) {
            return Astra::proto::RespBuilder::Error("Node not found for key '" + key + "'");
        }

        // 计算键的槽位 (使用CRC16算法，符合Redis规范)
        uint16_t slot = Astra::utils::CRC16::getKeyHashSlot(key);

        // 生成MOVED响应
        return "-MOVED " + std::to_string(slot) + " " + node->host + ":" + std::to_string(node->client_port) + "\r\n";
    }

    void ClusterSession::ProcessGossip(const std::string &gossip_data) {
        // 解析gossip数据
        // 假设gossip_data是RESP数组格式，这里简化处理
        // 实际实现中需要根据RESP协议解析

        // 使用ClusterManager中处理gossip信息
        cluster_manager_->ProcessGossipInfo(gossip_data);
    }

    void ClusterSession::SetClusterCommunicator(ClusterCommunicator *communicator) {
        cluster_communicator_ = communicator;
        ZEN_LOG_DEBUG("Cluster communicator set in ClusterSession: {}", communicator ? "success" : "failed");
    }
}// namespace Astra::cluster