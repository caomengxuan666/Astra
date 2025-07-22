#include "ChannelManager.hpp"
#include "core/astra.hpp"
#include "logger.hpp"
#include "session.hpp"
namespace Astra::apps {

    // WeakPtrHash的实现
    size_t ChannelManager::WeakPtrHash::operator()(const std::weak_ptr<Session> &ptr) const {
        // 锁定弱指针获取共享指针，计算原始指针的哈希
        if (auto shared = ptr.lock()) {
            return std::hash<std::uintptr_t>{}(
                    reinterpret_cast<std::uintptr_t>(shared.get()));
        }
        return 0;// 已过期的弱指针返回固定哈希值
    }

    // WeakPtrEqual的实现
    bool ChannelManager::WeakPtrEqual::operator()(const std::weak_ptr<Session> &lhs, const std::weak_ptr<Session> &rhs) const {
        // 同时锁定两个弱指针，比较原始指针是否相等
        auto lhs_shared = lhs.lock();
        auto rhs_shared = rhs.lock();
        if (!lhs_shared || !rhs_shared) {
            return false;// 任一已过期则不相等
        }
        return lhs_shared.get() == rhs_shared.get();
    }

    // 订阅频道的实现
    void ChannelManager::Subscribe(const std::string &channel, std::weak_ptr<Session> session) {
        std::unique_lock lock(mtx_);
        subscribers_[channel].insert(session);
        ZEN_LOG_DEBUG("Session subscribed to channel: {}", channel);
    }

    // 取消订阅的实现
    void ChannelManager::Unsubscribe(const std::string &channel, std::weak_ptr<Session> session) {
        std::unique_lock lock(mtx_);
        if (auto it = subscribers_.find(channel); it != subscribers_.end()) {
            it->second.erase(session);
            ZEN_LOG_DEBUG("Session unsubscribed from channel: {}", channel);

            // 若频道无订阅者，移除该频道
            if (it->second.empty()) {
                subscribers_.erase(it);
                ZEN_LOG_DEBUG("Channel removed (no subscribers): {}", channel);
            }
        }
    }

    // 发布消息的实现
    // ChannelManager.cpp
    size_t ChannelManager::Publish(const std::string &channel, const std::string &message) {
        size_t delivered_count = 0;

        // 步骤1：向精确频道订阅者推送（普通消息，无模式）
        {
            std::shared_lock lock(mtx_);
            auto it = subscribers_.find(channel);
            if (it != subscribers_.end()) {
                auto sessions = it->second;
                lock.unlock();
                for (const auto &weak_session: sessions) {
                    if (auto session = weak_session.lock()) {
                        session->PushMessage(channel, message, "");// 普通消息，模式为空
                        delivered_count++;
                    }
                }
            }
        }

        // 步骤2：向模式订阅者推送（传入匹配的模式）
        {
            std::shared_lock lock(mtx_);
            for (const auto &[pattern, sessions]: pattern_subscribers_) {
                if (MatchPattern(pattern, channel)) {
                    for (const auto &session: sessions) {
                        session->PushMessage(channel, message, pattern);// 传入匹配的模式
                        delivered_count++;
                    }
                }
            }
        }

        return delivered_count;
    }

    void ChannelManager::PSubscribe(const std::string &pattern, std::shared_ptr<Session> session) {
        std::unique_lock lock(mtx_);// 写锁：修改订阅者列表时需要独占访问
        // 将会话添加到模式的订阅者集合中
        pattern_subscribers_[pattern].insert(session);
        ZEN_LOG_DEBUG("Session subscribed to pattern: {}", pattern);
    }

    void ChannelManager::PUnsubscribe(const std::string &pattern, std::shared_ptr<Session> session) {
        std::unique_lock lock(mtx_);// 写锁：修改订阅者列表时需要独占访问
        auto it = pattern_subscribers_.find(pattern);
        if (it != pattern_subscribers_.end()) {
            // 从模式的订阅者集合中移除会话
            it->second.erase(session);
            ZEN_LOG_DEBUG("Session unsubscribed from pattern: {}", pattern);

            // 如果模式的订阅者集合为空，移除该模式以节省内存
            if (it->second.empty()) {
                pattern_subscribers_.erase(it);
                ZEN_LOG_DEBUG("Pattern removed (no subscribers): {}", pattern);
            }
        }
    }

    std::vector<std::string> ChannelManager::GetActiveChannels(const std::string &pattern) {
        std::vector<std::string> result;
        std::shared_lock lock(mtx_);// 读锁：仅读取数据，允许多线程并发访问

        // 遍历所有频道，过滤出符合模式的活跃频道（有订阅者的频道）
        for (const auto &[channel, subscribers]: subscribers_) {
            // 清理过期的弱指针（避免统计已销毁的会话）
            CleanupExpiredSubscribers(channel);

            // 如果频道有有效订阅者，且匹配模式，则加入结果
            if (!subscribers.empty() && MatchPattern(pattern, channel)) {
                result.push_back(channel);
            }
        }

        return result;
    }

    size_t ChannelManager::GetSubscriberCount(const std::string &channel) {
        std::shared_lock lock(mtx_);// 读锁
        auto it = subscribers_.find(channel);
        if (it == subscribers_.end()) {
            return 0;// 频道不存在，返回0
        }

        // 清理过期的弱指针，确保统计有效订阅者
        CleanupExpiredSubscribers(channel);
        return it->second.size();// 剩余有效订阅者数量
    }


    size_t ChannelManager::GetPatternSubscriberCount() {
        std::shared_lock lock(mtx_);
        std::unordered_set<std::shared_ptr<Session>> unique_subscribers;

        for (const auto &[pattern, subscribers]: pattern_subscribers_) {
            // 遍历订阅者时，过滤已过期的会话（关键修复）
            for (const auto &session: subscribers) {
                if (session) {// 确保会话未失效
                    unique_subscribers.insert(session);
                }
            }
        }

        return unique_subscribers.size();
    }

    void ChannelManager::CleanupExpiredSubscribers(const std::string &channel) {
        auto it = subscribers_.find(channel);
        if (it == subscribers_.end()) return;

        // 遍历订阅者列表，移除过期的弱指针
        auto &subscribers = it->second;
        for (auto iter = subscribers.begin(); iter != subscribers.end();) {
            if (iter->expired()) {
                iter = subscribers.erase(iter);// 移除过期会话
            } else {
                ++iter;
            }
        }

        // 如果清理后频道无订阅者，移除该频道
        if (subscribers.empty()) {
            subscribers_.erase(it);
        }
    }

    bool ChannelManager::MatchPattern(const std::string &pattern, const std::string &channel) const {
        // 简化版模式匹配：* 匹配任意字符序列（不支持 ? 等其他通配符，可根据需要扩展）
        size_t p = 0, c = 0;
        const size_t pattern_len = pattern.size();
        const size_t channel_len = channel.size();

        while (p < pattern_len && c < channel_len) {
            if (pattern[p] == '*') {
                // * 匹配任意字符，跳过所有字符直到下一个非*字符
                p++;
                if (p == pattern_len) {
                    return true;// 模式以*结尾，匹配剩余所有字符
                }
                // 寻找频道中匹配模式剩余部分的位置
                while (c < channel_len && channel[c] != pattern[p]) {
                    c++;
                }
            } else {
                // 普通字符必须完全匹配
                if (pattern[p] != channel[c]) {
                    return false;
                }
                p++;
                c++;
            }
        }

        // 处理剩余的*
        while (p < pattern_len && pattern[p] == '*') {
            p++;
        }

        // 模式和频道都遍历完才算匹配成功
        return p == pattern_len && c == channel_len;
    }

    std::vector<std::pair<std::string, size_t>> ChannelManager::GetActivePatterns() const {
        std::shared_lock lock(mtx_);
        std::vector<std::pair<std::string, size_t>> patterns;

        for (const auto &[pattern, sessions]: pattern_subscribers_) {
            // 统计有效会话数（过滤已断开的连接）
            size_t valid_count = 0;
            for (const auto &session: sessions) {
                if (session) valid_count++;
            }
            if (valid_count > 0) {
                patterns.emplace_back(pattern, valid_count);
            }
        }
        return patterns;
    }

    // 实现PUBSUB CHANNELS：返回匹配pattern的所有频道
    std::vector<std::string> ChannelManager::GetChannelsByPattern(const std::string &pattern) const {
        std::shared_lock lock(mtx_);
        std::vector<std::string> matched_channels;

        // 遍历所有普通频道（subscribers_的key是频道名）
        for (const auto &[channel, sessions]: subscribers_) {
            // 检查频道是否匹配模式（使用已有的MatchPattern函数）
            if (MatchPattern(pattern, channel)) {
                matched_channels.push_back(channel);
            }
        }

        return matched_channels;
    }

    // 实现PUBSUB NUMSUB：返回指定频道的订阅者数量
    // 例如在 GetChannelSubscriberCount 中（原代码可能用了 weak_ptr 直接判断）
    size_t ChannelManager::GetChannelSubscriberCount(const std::string &channel) const {
        std::shared_lock lock(mtx_);
        auto it = subscribers_.find(channel);
        if (it == subscribers_.end()) {
            return 0;
        }

        size_t valid_count = 0;
        for (const auto &weak_session: it->second) {// 假设存储的是 weak_ptr
            // 修复：通过 lock() 转换为 shared_ptr，判断是否有效
            if (auto session = weak_session.lock()) {// 有效则计数
                valid_count++;
            }
        }
        return valid_count;
    }

}// namespace Astra::apps
