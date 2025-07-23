#pragma once
#include <mutex>
#include <network/Singleton.h>
#include <string>
#include <unordered_map>
#include <vector>


namespace Astra::stats {

    // 事件类型定义
    enum class EventType {
        CONNECTION_OPENED,// 新连接建立
        CONNECTION_CLOSED,// 连接关闭
        COMMAND_PROCESSED // 命令处理完成
    };

    // 事件基类
    struct Event {
        virtual ~Event() = default;
        EventType type;
        explicit Event(EventType t) : type(t) {}
    };

    // 连接事件（包含会话ID等信息）
    struct ConnectionEvent : Event {
        std::string session_id;
        ConnectionEvent(EventType t, std::string id)
            : Event(t), session_id(std::move(id)) {}
    };

    // 命令事件（包含命令名称等信息）
    struct CommandEvent : Event {
        std::string command;
        size_t arg_count;
        CommandEvent(std::string cmd, size_t args)
            : Event(EventType::COMMAND_PROCESSED),
              command(std::move(cmd)),
              arg_count(args) {}
    };

    // 事件观察者接口
    class IStatsObserver {
    public:
        virtual ~IStatsObserver() = default;
        virtual void onEvent(const Event &event) = 0;
    };

    // 全局事件中心（单例）
    class EventCenter : public Singleton<EventCenter> {
    public:
        friend class Singleton<EventCenter>;
        // 注册观察者
        void registerObserver(EventType type, IStatsObserver *observer) {
            std::lock_guard<std::mutex> lock(mutex_);
            observers_[type].push_back(observer);
        }

        // 发送事件
        void postEvent(const Event &event) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = observers_.find(event.type);
            if (it != observers_.end()) {
                for (auto observer: it->second) {
                    observer->onEvent(event);// 同步通知观察者
                }
            }
        }

    private:
        EventCenter() = default;

        std::mutex mutex_;
        std::unordered_map<EventType, std::vector<IStatsObserver *>> observers_;
    };

    // 事件发送工具函数（简化调用）
    inline void emitConnectionOpened(const std::string &session_id) {
        EventCenter::GetInstance()->postEvent(
                ConnectionEvent(EventType::CONNECTION_OPENED, session_id));
    }

    inline void emitConnectionClosed(const std::string &session_id) {
        EventCenter::GetInstance()->postEvent(
                ConnectionEvent(EventType::CONNECTION_CLOSED, session_id));
    }

    inline void emitCommandProcessed(const std::string &cmd, size_t arg_count) {
        EventCenter::GetInstance()->postEvent(
                CommandEvent(cmd, arg_count));
    }

}// namespace Astra::stats
