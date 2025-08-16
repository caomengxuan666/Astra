#pragma once

#include "ClusterCommunicator.hpp"
#include "ClusterManager.hpp"
#include "datastructures/lru_cache.hpp"
#include <memory>
#include <string>

namespace Astra::cluster {

    class ClusterSession {
    public:
        ClusterSession(std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache);

        // 设置ClusterCommunicator引用
        void SetClusterCommunicator(ClusterCommunicator *communicator);

        // 处理客户端请求
        std::string ProcessClientRequest(const std::string &command, const std::vector<std::string> &args);

        // 检查键是否在当前节点
        bool IsKeyLocal(const std::string &key) const;

        // 重定向客户端到正确的节点
        std::string GenerateRedirectResponse(const std::string &key) const;

        // 处理Gossip信息
        void ProcessGossip(const std::string &gossip_data);

    private:
        std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache_;
        std::shared_ptr<ClusterManager> cluster_manager_;
        ClusterCommunicator *cluster_communicator_ = nullptr;// 弱引用，避免循环依赖
    };

}// namespace Astra::cluster