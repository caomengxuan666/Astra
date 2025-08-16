#include "ClusterManager.hpp"
#include "utils/CRC16.hpp"
#include <algorithm>
#include <cstring>// For std::memcpy
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>


namespace Astra::cluster {

    std::shared_ptr<ClusterManager> ClusterManager::GetInstance() {
        static std::shared_ptr<ClusterManager> instance = std::shared_ptr<ClusterManager>(new ClusterManager());
        return instance;
    }

    bool ClusterManager::Initialize(const std::string &local_host, uint16_t local_port) {
        local_host_ = local_host;
        local_port_ = local_port;

        // 清空现有节点，确保每次启动都是干净的状态
        nodes_.clear();

        // 生成本地节点ID (使用随机数，符合Redis规范)
        local_node_id_ = GenerateRandomNodeId();

        // 添加本地节点 (客户端端口和集群端口)
        // 修复：正确设置集群端口为客户端端口+10000
        AddNode(local_node_id_, local_host, local_port, static_cast<uint16_t>(local_port + 10000), true);

        // 初始化槽位映射
        slot_to_node_.resize(SLOT_COUNT);

        return true;
    }

    bool ClusterManager::AddNode(const std::string &node_id, const std::string &host, uint16_t client_port, uint16_t cluster_port, bool is_master) {
        ZEN_LOG_DEBUG("Attempting to add node: id={}, host={}, client_port={}, cluster_port={}, is_master={}",
                      node_id, host, client_port, cluster_port, is_master);

        // 检查节点是否已存在（通过node_id判断）
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            // 节点已存在，更新信息
            ZEN_LOG_DEBUG("Node {} already exists, updating info", node_id);
            it->second->host = host;
            it->second->client_port = client_port;
            it->second->cluster_port = cluster_port;
            it->second->is_master = is_master;
            ZEN_LOG_INFO("Updated existing node {} at {}:{}@{}", node_id, host, client_port, cluster_port);
            return true;
        }

        // 检查是否有使用相同host和端口的节点（防止重复添加）
        for (const auto &pair: nodes_) {
            const auto &existing_node = pair.second;
            if (existing_node->host == host && existing_node->client_port == client_port) {
                ZEN_LOG_WARN("Node with host {} and port {} already exists with id {}", host, client_port, existing_node->id);
                return false;// 节点已存在
            }
        }

        // 创建集群节点（client_port为客户端端口，cluster_port为集群通信端口）
        auto node = std::make_shared<ClusterNode>();
        node->id = node_id;
        node->host = host;
        node->client_port = client_port;
        node->cluster_port = cluster_port;// 正确设置集群端口
        node->is_master = is_master;
        node->fail_flag = false;
        node->fail_confirmed = false;
        node->last_ping_time = 0;
        node->last_pong_time = 0;
        node->config_epoch = 0;

        nodes_[node_id] = node;
        node_to_slots_[node_id] = {};// 初始化空的槽位集合

        ZEN_LOG_INFO("Added node {} at {}:{}@{}", node_id, host, client_port, cluster_port);
        return true;
    }

    // 为了保持向后兼容性，保留旧的AddNode方法
    bool ClusterManager::AddNode(const std::string &node_id, const std::string &host, uint16_t port, bool is_master) {
        // 默认集群端口为客户端端口+10000（偏移量定义在ClusterManager.hpp中）
        return AddNode(node_id, host, port, port + 10000, is_master);
    }

    void ClusterManager::UpdateNodePorts(const std::string &node_id, uint16_t client_port, uint16_t cluster_port) {
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            it->second->client_port = client_port;
            it->second->cluster_port = cluster_port;
        }
    }

    bool ClusterManager::RemoveNode(const std::string &node_id) {
        auto it = nodes_.find(node_id);
        if (it == nodes_.end()) {
            return false;// 节点不存在
        }

        // 移除节点负责的槽位
        node_to_slots_.erase(node_id);

        // 从节点映射中移除
        nodes_.erase(it);

        ZEN_LOG_INFO("Removed node {}", node_id);
        return true;
    }

    std::shared_ptr<ClusterNode> ClusterManager::GetNode(const std::string &node_id) const {
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            return it->second;
        }
        return {};
    }

    std::string ClusterManager::GenerateRandomNodeId() {
        // 使用静态引擎避免重复初始化
        static std::mt19937 gen([]() {
            // 使用random_device获取随机种子
            std::random_device rd;

            // 获取更多熵值，避免时间戳初始化导致的重复性
            std::array<uint32_t, std::mt19937::state_size> seed_data;
            std::generate(seed_data.begin(), seed_data.end(), std::ref(rd));

            // 使用种子数据初始化梅森旋转引擎
            std::seed_seq seq(seed_data.begin(), seed_data.end());
            return std::mt19937(seq);
        }());

        std::uniform_int_distribution<> dis(0, 15);

        std::string id;
        id.reserve(40);

        for (int i = 0; i < 40; ++i) {
            id += "0123456789abcdef"[dis(gen)];
        }

        return id;
    }

    bool ClusterManager::SaveNodesConfig(const std::string &filepath) {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        // 保存当前配置纪元
        file << "current_epoch:" << current_epoch_ << "\n";

        // 保存所有节点信息
        for (const auto &pair: nodes_) {
            const auto &node = pair.second;
            file << node->id << " "
                 << node->host << ":" << node->client_port << "@" << node->cluster_port << " "
                 << (node->is_master ? "master" : "slave") << " "
                 << "-" << " "// master ID (从节点使用)
                 << "0" << " "// ping发送时间
                 << "0" << " "// pong接收时间
                 << node->config_epoch << " "
                 << "connected" << " ";// 连接状态

            // 保存槽位范围
            auto slot_ranges = GetSlotRangesForNode(node->id);
            for (size_t i = 0; i < slot_ranges.size(); ++i) {
                const auto &range = slot_ranges[i];
                file << range.first;
                if (range.first != range.second) {
                    file << "-" << range.second;
                }
                if (i < slot_ranges.size() - 1) {
                    file << " ";
                }
            }
            file << "\n";
        }

        file.close();
        return true;
    }

    bool ClusterManager::LoadNodesConfig(const std::string &filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        bool first_line = true;

        while (std::getline(file, line)) {
            if (line.empty()) continue;

            if (first_line && line.substr(0, 15) == "current_epoch:") {
                // 读取当前配置纪元
                current_epoch_ = std::stoull(line.substr(15));
                first_line = false;
                continue;
            }

            first_line = false;

            std::istringstream iss(line);
            std::string node_id, addr_info, flags, master_id, ping_sent, pong_recv, epoch_str, link_state, slots;

            iss >> node_id >> addr_info >> flags >> master_id >> ping_sent >> pong_recv >> epoch_str >> link_state;

            // 解析地址信息 host:client_port@cluster_port
            size_t colon_pos = addr_info.find(':');
            size_t at_pos = addr_info.find('@');

            if (colon_pos != std::string::npos && at_pos != std::string::npos) {
                std::string host = addr_info.substr(0, colon_pos);
                uint16_t client_port = static_cast<uint16_t>(std::stoi(addr_info.substr(colon_pos + 1, at_pos - colon_pos - 1)));
                uint16_t cluster_port = static_cast<uint16_t>(std::stoi(addr_info.substr(at_pos + 1)));

                bool is_master = (flags == "master");

                // 添加所有节点信息，而不仅仅是本地节点
                AddNode(node_id, host, client_port, cluster_port, is_master);

                // 更新节点配置纪元
                if (auto node = GetNode(node_id)) {
                    node->config_epoch = std::stoull(epoch_str);
                }

                // 如果是本地节点，更新本地节点ID
                if (host == local_host_ && client_port == local_port_) {
                    local_node_id_ = node_id;
                }
            }
        }

        file.close();
        return !nodes_.empty();// 如果加载了节点信息则返回true
    }

    void ClusterManager::UpdateNodePingTime(const std::string &node_id) {
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            it->second->last_ping_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::system_clock::now().time_since_epoch())
                                                 .count();
        }
    }

    void ClusterManager::UpdateNodePongTime(const std::string &node_id) {
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            it->second->last_pong_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::system_clock::now().time_since_epoch())
                                                 .count();
        }
    }

    void ClusterManager::SetNodeFailFlag(const std::string &node_id, bool fail) {
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            it->second->fail_flag = fail;
        }
    }

    void ClusterManager::SetNodeFailConfirmed(const std::string &node_id, bool confirmed) {
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            it->second->fail_confirmed = confirmed;
        }
    }

    void ClusterManager::ProcessGossipInfo(const std::string &gossip_data) {
        // 解析gossip信息并更新节点状态
        std::istringstream iss(gossip_data);
        std::string node_info;

        while (std::getline(iss, node_info, ';')) {
            if (node_info.empty()) continue;

            // 解析单个节点信息: node_id,host,port,config_epoch,flags
            std::istringstream node_iss(node_info);
            std::string token;
            std::vector<std::string> tokens;

            while (std::getline(node_iss, token, ',')) {
                tokens.push_back(token);
            }

            // 至少需要 5 个字段
            if (tokens.size() >= 5) {
                std::string node_id = tokens[0];
                std::string host = tokens[1];
                uint16_t client_port = 0;

                try {
                    client_port = static_cast<uint16_t>(std::stoi(tokens[2]));
                } catch (const std::exception &e) {
                    ZEN_LOG_WARN("Invalid port in gossip info: {}", tokens[2]);
                    continue;
                }

                uint64_t config_epoch = 0;
                try {
                    config_epoch = std::stoull(tokens[3]);
                } catch (const std::exception &e) {
                    ZEN_LOG_WARN("Invalid epoch in gossip info: {}", tokens[3]);
                    continue;
                }

                std::string flags = tokens[4];

                // 检查节点是否已存在
                auto existing_node = GetNode(node_id);
                if (!existing_node) {
                    // 新节点，添加到集群
                    bool is_master = (flags.find("master") != std::string::npos);
                    AddNode(node_id, host, client_port, is_master);
                    ZEN_LOG_INFO("Added new node {} from gossip info", node_id);
                } else {
                    // 更新现有节点信息（如果 config_epoch 更高）
                    if (config_epoch > existing_node->config_epoch) {
                        existing_node->host = host;
                        existing_node->client_port = client_port;
                        existing_node->cluster_port = client_port + 10000;
                        existing_node->config_epoch = config_epoch;
                        existing_node->is_master = (flags.find("master") != std::string::npos);

                        // 处理故障标记
                        existing_node->fail_flag = (flags.find("fail?") != std::string::npos);
                        existing_node->fail_confirmed = (flags.find("fail") != std::string::npos);

                        ZEN_LOG_INFO("Updated node {} from gossip info", node_id);
                    }
                }
            }
        }
    }

    void ClusterManager::AddSlotRange(uint16_t start_slot, uint16_t end_slot, const std::string &node_id) {
        // 添加槽位范围到指定节点
        for (uint16_t slot = start_slot; slot <= end_slot; ++slot) {
            if (slot < slot_to_node_.size()) {
                slot_to_node_[slot] = node_id;
            }
        }

        // 更新节点到槽位的映射
        for (uint16_t slot = start_slot; slot <= end_slot; ++slot) {
            node_to_slots_[node_id].insert(slot);
        }
    }

    std::vector<std::pair<uint16_t, uint16_t>> ClusterManager::GetSlotRangesForNode(const std::string &node_id) const {
        std::vector<std::pair<uint16_t, uint16_t>> ranges;
        const auto &slots = node_to_slots_.at(node_id);

        if (slots.empty()) {
            return ranges;
        }

        auto it = slots.begin();
        uint16_t start = *it;
        uint16_t end = start;
        ++it;

        while (it != slots.end()) {
            if (*it == end + 1) {
                // 连续的槽位
                end = *it;
            } else {
                // 不连续，保存当前范围
                ranges.push_back({start, end});
                start = end = *it;
            }
            ++it;
        }

        // 添加最后一个范围
        ranges.push_back({start, end});
        return ranges;
    }

    std::string ClusterManager::GetNodeForKey(const std::string &key) const {
        // 计算键的哈希槽
        uint16_t slot = utils::CRC16::getKeyHashSlot(key) % SLOT_COUNT;

        // 检查槽位是否已分配
        if (slot < slot_to_node_.size()) {
            return slot_to_node_[slot];
        }

        // 如果槽位未分配，返回空字符串
        return "";
    }

    std::string ClusterManager::GetGossipInfo() const {
        std::ostringstream oss;
        for (const auto &pair: nodes_) {
            const auto &node = pair.second;
            if (node->id == local_node_id_) continue;

            oss << node->id << ","
                << node->host << ","
                << node->client_port << ","
                << node->config_epoch << ","
                << (node->is_master ? "master" : "slave");

            if (node->fail_confirmed) oss << ",fail";
            else if (node->fail_flag)
                oss << ",fail?";

            oss << ";";
        }
        return oss.str();
    }
}// namespace Astra::cluster