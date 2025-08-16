#pragma once

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Astra::cluster {

    struct ClusterNode {
        std::string id;         // 节点ID (40个字符的十六进制字符串)
        std::string host;       // 节点主机地址
        uint16_t client_port;   // 客户端端口
        uint16_t cluster_port;  // 集群总线端口 (客户端端口+10000)
        bool is_master;         // 是否为主节点
        uint64_t config_epoch;  // 配置纪元，用于解决冲突
        uint64_t last_ping_time;// 最后一次ping时间戳
        uint64_t last_pong_time;// 最后一次pong时间戳
        bool fail_flag;         // PFAIL标志
        bool fail_confirmed;    // FAIL标志（确认下线）

        // 添加默认构造函数
        ClusterNode() : client_port(0), cluster_port(0), is_master(false),
                        config_epoch(0), last_ping_time(0), last_pong_time(0),
                        fail_flag(false), fail_confirmed(false) {}

        ClusterNode(const std::string &node_id,
                    const std::string &node_host,
                    uint16_t c_port,
                    uint16_t cl_port,
                    bool master)
            : id(node_id), host(node_host), client_port(c_port), cluster_port(cl_port),
              is_master(master), config_epoch(0), last_ping_time(0), last_pong_time(0),
              fail_flag(false), fail_confirmed(false) {}
    };

    // Redis集群哈希槽总数
    constexpr uint16_t SLOT_COUNT = 16384;

    // 集群槽位状态
    enum class SlotState {
        NONE,     // 未分配
        IMPORTING,// 正在导入
        MIGRATING // 正在迁移
    };

    // 槽位信息结构
    struct SlotInfo {
        uint16_t slot;
        std::string node_id;
        SlotState state;
        std::string migrating_to;  // 迁移到的节点ID
        std::string importing_from;// 从哪个节点导入
    };

    class ClusterManager {
    public:
        static std::shared_ptr<ClusterManager> GetInstance();

        // 初始化集群管理器
        bool Initialize(const std::string &local_host, uint16_t local_port);

        // 添加节点到集群
        bool AddNode(const std::string &node_id, const std::string &host, uint16_t client_port, uint16_t cluster_port, bool is_master);
        bool AddNode(const std::string &node_id, const std::string &host, uint16_t port, bool is_master);// 为了向后兼容

        // 更新节点端口信息
        void UpdateNodePorts(const std::string &node_id, uint16_t client_port, uint16_t cluster_port);

        // 移除节点
        bool RemoveNode(const std::string &node_id);

        // 更新节点的ping时间
        void UpdateNodePingTime(const std::string &node_id);

        // 更新节点的pong时间
        void UpdateNodePongTime(const std::string &node_id);

        // 设置节点失败标志
        void SetNodeFailFlag(const std::string &node_id, bool fail);

        // 设置节点确认失败标志
        void SetNodeFailConfirmed(const std::string &node_id, bool fail);

        // 获取本地节点ID
        const std::string &GetLocalNodeId() const { return local_node_id_; }

        // 获取本地主机地址
        const std::string &GetLocalHost() const { return local_host_; }

        // 获取本地客户端端口
        uint16_t GetLocalClientPort() const { return local_port_; }

        // 获取所有节点
        const std::unordered_map<std::string, std::shared_ptr<ClusterNode>> &GetAllNodes() const { return nodes_; }

        // 获取节点
        std::shared_ptr<ClusterNode> GetNode(const std::string &node_id) const;

        // 添加槽位范围到节点
        void AddSlotRange(uint16_t start_slot, uint16_t end_slot, const std::string &node_id);

        // 获取节点负责的槽位范围
        std::vector<std::pair<uint16_t, uint16_t>> GetSlotRangesForNode(const std::string &node_id) const;

        // 保存节点配置到文件
        bool SaveNodesConfig(const std::string &filepath);

        // 从文件加载节点配置
        bool LoadNodesConfig(const std::string &filepath);

        // 处理gossip信息
        void ProcessGossipInfo(const std::string &gossip_data);

        // 生成随机节点ID
        std::string GenerateRandomNodeId();

        // 获取当前配置纪元
        uint64_t GetCurrentEpoch() const { return current_epoch_; }

        // 根据键获取对应的节点
        std::string GetNodeForKey(const std::string &key) const;

        // 获取Gossip信息
        std::string GetGossipInfo() const;

    private:
        std::unordered_map<std::string, std::shared_ptr<ClusterNode>> nodes_;
        std::unordered_map<std::string, std::set<uint16_t>> node_to_slots_;
        std::vector<std::string> slot_to_node_;// 槽位到节点ID的映射
        std::string local_node_id_;
        std::string local_host_;
        uint16_t local_port_;
        uint64_t current_epoch_;

        ClusterManager() = default;                                // 私有构造函数确保单例模式
        ClusterManager(const ClusterManager &) = delete;           // 禁止拷贝构造
        ClusterManager &operator=(const ClusterManager &) = delete;// 禁止赋值操作
    };

}// namespace Astra::cluster