#pragma once

#include "concurrent/task_queue.hpp"
#include "datastructures/lockfree_queue.hpp"
#include "datastructures/lru_cache.hpp"
#include "server/ChannelManager.hpp"
#include <asio.hpp>
#include <asio/strand.hpp>
#include <memory>
#include <unordered_set>
#include <vector>

// 前置声明，减少头文件依赖
namespace Astra::proto {
    class RedisCommandHandler;
    class RespBuilder;
}// namespace Astra::proto

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
        void PushMessage(const std::string &channel, const std::string &message);
        void PushMessage(const std::string &channel, const std::string &message, const std::string &pattern);
        const std::unordered_set<std::string> &GetSubscribedChannels() const;

        void AddSubscribedPattern(const std::string &pattern) {
            subscribed_patterns_.insert(pattern);
        }

        void RemoveSubscribedPattern(const std::string &pattern) {
            subscribed_patterns_.erase(pattern);
        }

        void ClearSubscribedPatterns() {
            if (subscribed_patterns_.empty()) return;

            // 关键：通知 ChannelManager 移除所有模式的订阅关系
            for (const auto &pattern: subscribed_patterns_) {
                channel_manager_->PUnsubscribe(pattern, shared_from_this());
            }
            subscribed_patterns_.clear();// 清空本地记录
        }

        const std::unordered_set<std::string> &GetSubscribedPatterns() const {
            return subscribed_patterns_;
        }

        void SwitchMode(SessionMode new_mode);

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
        std::shared_ptr<proto::RedisCommandHandler> handler_;
        std::shared_ptr<concurrent::TaskQueue> task_queue_;
        ParseState parse_state_;
        int remaining_args_;
        int current_bulk_size_;
        std::vector<std::string> argv_;
        std::unordered_set<std::string> subscribed_channels_;
        std::unordered_set<std::string> subscribed_patterns_;// 存储已订阅的模式
        datastructures::LockFreeQueue<PubSubMessage, 4096, datastructures::OverflowPolicy::RESIZE> msg_queue_;
        std::atomic_bool is_writing_;
        std::shared_ptr<ChannelManager> channel_manager_;
        std::string session_id_;

        // 私有成员函数声明

        void DoRead();
        void DoReadPubSub();
        size_t ProcessBuffer();
        void HandleArrayHeader(const std::string &line);
        void HandleBulkHeader(const std::string &line);
        void ReadBulkContent();
        void HandleBulkContent();
        void ProcessRequest();
        void HandlePubSubCommand();
        void TriggerMessageWrite();
        void DoWriteMessages();
        void WriteResponse(const std::string &response);
        void CleanupSubscriptions();
    };

}// namespace Astra::apps