#pragma once
#include "stats_event.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <network/Singleton.h>
#include <thread>


#ifdef _WIN32
#include <psapi.h>
#include <windows.h>

#pragma comment(lib, "psapi.lib")// Windows 链接 PSAPI 库
#else
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>

#endif

namespace Astra::apps {
    struct ServerStatus;
}

namespace Astra::apps {
    class StatusCollector : public Singleton<StatusCollector>, public stats::IStatsObserver {
    public:
        friend class Singleton<StatusCollector>;
        void collectDynamicStatus();
        void collectStaticStatus();

        // 线程控制接口
        void start(std::chrono::milliseconds interval = std::chrono::seconds(1));
        void stop();
        bool isRunning() const { return m_running.load(); }

        // 实现观察者接口
        void onEvent(const stats::Event &event) override;
        ~StatusCollector();

    private:
        StatusCollector();// 构造函数声明（实现移至cpp）
        // 线程主函数
        void threadMain();

        void collectClients();
        void collectMemories();
        void collectStats();
        void collectCpu();

        // 线程控制变量（原子类型确保线程安全）
        std::atomic<bool> m_running{false};
        std::atomic<bool> m_stopRequested{false};
        std::chrono::milliseconds m_interval{std::chrono::seconds(1)};

        // 线程及同步对象
        std::thread m_thread;
        std::mutex m_mutex;
        std::condition_variable m_cv;

        // 统计数据（线程安全）
        std::mutex stats_mutex_;
        size_t total_connections_received_ = 0;
        size_t total_commands_processed_ = 0;
        std::unordered_set<std::string> active_sessions_;// 跟踪活跃连接
        std::chrono::steady_clock::time_point server_start_time;

        // ServerStatus引用（在构造函数中初始化）
        ServerStatus &m_status;
    };
}// namespace Astra::apps
