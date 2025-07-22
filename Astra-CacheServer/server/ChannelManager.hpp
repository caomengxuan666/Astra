#pragma once

#include "network/Singleton.h"
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

// 前置声明，解决循环依赖
namespace Astra::apps {
    class Session;
}

namespace Astra::apps {

    // 继承自Singleton模板类，实现单例模式
    class ChannelManager : public Singleton<ChannelManager> {
    public:
        // 允许Singleton访问私有构造函数
        friend class Singleton<ChannelManager>;

        // 订阅频道：将会话添加到频道的订阅者列表
        void Subscribe(const std::string &channel, std::weak_ptr<Session> session);

        // 取消订阅：从频道的订阅者列表移除会话
        void Unsubscribe(const std::string &channel, std::weak_ptr<Session> session);

        // 模式订阅相关
        void PSubscribe(const std::string &pattern, std::shared_ptr<Session> session);
        void PUnsubscribe(const std::string &pattern, std::shared_ptr<Session> session);

        // PUBSUB 命令所需接口
        std::vector<std::string> GetActiveChannels(const std::string &pattern = "*");// 支持模式过滤
        size_t GetSubscriberCount(const std::string &channel);                       // 频道的订阅者数量
        size_t GetPatternSubscriberCount();                                          // 模式订阅的总数

        // 发布消息：向频道所有订阅者推送消息
        size_t Publish(const std::string &channel, const std::string &message);

        // 新增：获取匹配模式的所有频道（用于PUBSUB CHANNELS）
        std::vector<std::string> GetChannelsByPattern(const std::string &pattern) const;

        // 新增：获取指定频道的订阅者数量（用于PUBSUB NUMSUB）
        size_t GetChannelSubscriberCount(const std::string &channel) const;

        std::vector<std::pair<std::string, size_t>> GetActivePatterns() const;

    private:
        // 私有构造函数，确保只能通过Singleton创建
        ChannelManager() = default;

        void CleanupExpiredSubscribers(const std::string &channel);
        // 模式匹配（判断channel是否匹配pattern）
        bool MatchPattern(const std::string &pattern, const std::string &channel) const;

        // 自定义哈希函数：用于weak_ptr<Session>的哈希计算
        struct WeakPtrHash {
            size_t operator()(const std::weak_ptr<Session> &ptr) const;
        };

        // 自定义相等比较函数：用于weak_ptr<Session>的相等性判断
        struct WeakPtrEqual {
            bool operator()(const std::weak_ptr<Session> &lhs, const std::weak_ptr<Session> &rhs) const;
        };

        mutable std::shared_mutex mtx_;// 读写锁，支持多读者单写者
        // 存储结构：频道名称 -> 订阅者会话（弱指针集合）
        std::unordered_map<
                std::string,
                std::unordered_set<
                        std::weak_ptr<Session>,
                        WeakPtrHash,
                        WeakPtrEqual>>
                subscribers_;

        // 模式订阅存储：模式 -> 订阅者会话（强指针，确保模式订阅期间会话不被销毁）
        std::unordered_map<
                std::string,
                std::unordered_set<
                        std::shared_ptr<Session>,
                        std::hash<std::shared_ptr<Session>>>>
                pattern_subscribers_;// 新增：模式订阅者映射
    };

}// namespace Astra::apps
