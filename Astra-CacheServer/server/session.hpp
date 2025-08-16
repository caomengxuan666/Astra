#pragma once

#include "cluster/ClusterCommunicator.hpp"
#include "cluster/ClusterSession.hpp"
#include "concurrent/task_queue.hpp"
#include "datastructures/lockfree_queue.hpp"
#include "datastructures/lru_cache.hpp"
#include "logger.hpp"
#include "proto/ProtocolParser.hpp"
#include "server/ChannelManager.hpp"
#include <asio.hpp>
#include <asio/strand.hpp>
#include <memory>
#include <unordered_set>
#include <vector>

// 前置声明，减少头文件依赖
namespace Astra::proto {
    class RedisCommandHandler;
    class ProtocolParser;
    class RespBuilder;
}// namespace Astra::proto

namespace Astra::server {
    class CommandHandler;
}// namespace Astra::server

namespace Astra::cluster {
    class ClusterSession;
}// namespace Astra::cluster

namespace Astra::apps {
    // 消息结构体，用于存储模式匹配信息
    struct PubSubMessage {
        // 添加默认构造函数（无参数）
        PubSubMessage() = default;

        // 带参数的构造函数（用于创建消息时初始化）
        PubSubMessage(std::string channel, std::string content, std::string pattern)
            : channel(std::move(channel)),
              content(std::move(content)),
              pattern(std::move(pattern)) {}

        std::string channel;// 消息所属频道
        std::string content;// 消息内容
        std::string pattern;// 匹配的模式（为空表示普通消息）
    };

    // 新增PubSubSession类，专门处理PubSub相关功能
    class PubSubSession {
    public:
        explicit PubSubSession(std::shared_ptr<ChannelManager> channel_manager);

        void PushMessage(const std::string &channel, const std::string &message);
        void PushMessage(const std::string &channel, const std::string &message, const std::string &pattern);
        void AddSubscribedPattern(const std::string &pattern);
        void RemoveSubscribedPattern(const std::string &pattern);
        void ClearSubscribedPatterns();
        const std::unordered_set<std::string> &GetSubscribedPatterns() const;
        const std::unordered_set<std::string> &GetSubscribedChannels() const;
        void Subscribe(const std::string &channel);
        void Unsubscribe(const std::string &channel);
        void UnsubscribeAll();
        void PUnsubscribeAll();
        bool HasSubscriptions() const;

    private:
        std::shared_ptr<ChannelManager> channel_manager_;
        std::unordered_set<std::string> subscribed_channels_;
        std::unordered_set<std::string> subscribed_patterns_;
    };

    enum class SessionMode {
        CacheMode,
        PubSubMode
    };

    class Session : public std::enable_shared_from_this<Session> {
    public:
        // 构造函数声明
        explicit Session(
                asio::ip::tcp::socket socket,
                std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache,
                std::shared_ptr<concurrent::TaskQueue> global_task_queue,
                std::shared_ptr<ChannelManager> channel_manager);

        ~Session();

        // 公共接口声明
        void Start();
        void Stop();
        void PushMessage(const std::string &channel, const std::string &message, const std::string &pattern);
        void PushMessage(const std::string &channel, const std::string &message);
        const std::unordered_set<std::string> &GetSubscribedChannels() const;

        // 添加PubSub相关方法，供CommandImpl调用
        void AddSubscribedPattern(const std::string &pattern) { pubsub_session_->AddSubscribedPattern(pattern); }
        void RemoveSubscribedPattern(const std::string &pattern) { pubsub_session_->RemoveSubscribedPattern(pattern); }
        void ClearSubscribedPatterns() { pubsub_session_->ClearSubscribedPatterns(); }
        const std::unordered_set<std::string> &GetSubscribedPatterns() const { return pubsub_session_->GetSubscribedPatterns(); }

        void SwitchMode(SessionMode new_mode);

        // 添加集群通信器支持
        void SetClusterCommunicator(Astra::cluster::ClusterCommunicator *communicator) {
            cluster_communicator_ = communicator;
        }

    private:
        // 解析状态枚举
        enum class ParseState {
            ReadingArrayHeader,
            ReadingBulkHeader,
            ReadingBulkContent
        };

        // 私有成员变量（仅声明）
        SessionMode session_mode_;
        bool stopped_;
        asio::ip::tcp::socket socket_;
        asio::strand<asio::any_io_executor> strand_;
        std::string buffer_;
        std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache_;
        std::shared_ptr<proto::ProtocolParser> parser_;
        std::shared_ptr<server::CommandHandler> command_handler_;
        std::shared_ptr<proto::RedisCommandHandler> handler_;
        std::shared_ptr<concurrent::TaskQueue> task_queue_;
        std::vector<std::string> argv_;
        std::shared_ptr<PubSubSession> pubsub_session_;// 依赖注入的PubSubSession
        datastructures::LockFreeQueue<PubSubMessage, 4096, datastructures::OverflowPolicy::RESIZE> msg_queue_;
        datastructures::LockFreeQueue<std::string, 4096, datastructures::OverflowPolicy::RESIZE> cluster_bus_msg_queue_;
        std::atomic_bool is_writing_;
        std::atomic_bool is_writing_cluster_bus_;
        std::shared_ptr<ChannelManager> channel_manager_;
        std::string session_id_;
        std::shared_ptr<proto::RedisCommandHandler> redis_handler_;
        std::shared_ptr<cluster::ClusterSession> cluster_session_;
        Astra::cluster::ClusterCommunicator *cluster_communicator_;

        // 私有成员函数声明

        void DoRead();
        void DoReadPubSub();
        size_t ProcessBuffer();
        void ProcessRequest();
        // 处理PubSub命令的公共接口
        void HandlePubSubCommand();
        void TriggerMessageWrite();
        void DoWriteMessages();

        // 集群总线协议处理
        bool HandleClusterBusMessage(const std::string &message);
        void TriggerClusterBusWrite();
        void DoWriteClusterBusMessages();
        std::string HandleClusterCommand(const std::vector<std::string> &argv);
        // 写入响应的公共接口
        void WriteResponse(const std::string &response);
        void CleanupSubscriptions();
    };

}// namespace Astra::apps