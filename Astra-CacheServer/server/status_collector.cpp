#include "status_collector.h"
#include "config/ConfigManager.h"
#include "persistence/process.hpp"
#include "persistence/util_path.hpp"
#include "server_status.h"
#include "utils/uuid_utils.h"
#include "version_info.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

using namespace Astra::apps;
using namespace Astra::utils;
using namespace Astra::persistence;
using namespace std::chrono;

// 静态变量：进程信息收集的基准数据
static std::mutex proc_mutex;
static steady_clock::time_point last_collect_time;
static uint64_t last_proc_sys = 0; // 进程内核态时间基准（100纳秒）
static uint64_t last_proc_user = 0;// 进程用户态时间基准（100纳秒）
static uint64_t last_proc_sys_children = 0;
static uint64_t last_proc_user_children = 0;

// 系统CPU时间基准（用于Windows相对计算）
static uint64_t last_system_idle = 0;
static uint64_t last_system_kernel = 0;
static uint64_t last_system_user = 0;

// 服务器启动时间
static steady_clock::time_point server_start_time;


// 构造函数：初始化事件监听和基准时间
Astra::apps::StatusCollector::StatusCollector()
    : m_status(ServerStatusInstance::GetInstance()->getStatus()) {
    // 注册事件观察者
    stats::EventCenter::GetInstance()->registerObserver(
            stats::EventType::CONNECTION_OPENED, this);
    stats::EventCenter::GetInstance()->registerObserver(
            stats::EventType::CONNECTION_CLOSED, this);
    stats::EventCenter::GetInstance()->registerObserver(
            stats::EventType::COMMAND_PROCESSED, this);

    // 初始化服务器启动时间
    server_start_time = steady_clock::now();
}

// 事件处理：更新连接和命令统计
void StatusCollector::onEvent(const stats::Event &event) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    switch (event.type) {
        case stats::EventType::CONNECTION_OPENED: {
            const auto &conn_event = static_cast<const stats::ConnectionEvent &>(event);
            active_sessions_.insert(conn_event.session_id);
            total_connections_received_++;
            break;
        }
        case stats::EventType::CONNECTION_CLOSED: {
            const auto &conn_event = static_cast<const stats::ConnectionEvent &>(event);
            active_sessions_.erase(conn_event.session_id);
            break;
        }
        case stats::EventType::COMMAND_PROCESSED: {
            total_commands_processed_++;
            break;
        }
    }
}

// 收集客户端连接状态
void StatusCollector::collectClients() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    m_status.setConnectedClients(active_sessions_.size());
}

// 收集命令和连接统计
void StatusCollector::collectStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    m_status.setTotalConnectionsReceived(total_connections_received_)
            .setTotalCommandsProcessed(total_commands_processed_);
}

// 析构函数：确保线程正确停止
StatusCollector::~StatusCollector() {
    std::cout << "Stopping status collector..." << std::endl;
    if (isRunning()) {
        stop();
    }
}

// 启动收集线程
void StatusCollector::start(std::chrono::milliseconds interval) {
    if (m_running.load()) return;

    m_interval = interval;
    m_stopRequested.store(false);
    m_running.store(true);
    m_thread = std::thread(&StatusCollector::threadMain, this);
}

// 停止收集线程
void StatusCollector::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;

        m_stopRequested = true;
        m_running = false;
    }
    m_cv.notify_one();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// 线程主函数：定期收集动态状态
void StatusCollector::threadMain() {
    collectStaticStatus();// 初始化静态信息


    while (!m_stopRequested.load()) {
        collectDynamicStatus();// 收集动态信息

        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, m_interval, [this]() {
            return m_stopRequested.load();
        });
    }
}

// 收集静态状态信息（仅初始化一次）
void StatusCollector::collectStaticStatus() {
    // 基础信息
    std::string process_id = get_pid_str();
    uint16_t current_port = ConfigManager::GetInstance()->getListeningPort();
    std::string executablePath = getExecutableDirectory();

    // 生成run_id
    auto generator = UuidUtils::GetInstance().get_generator();
    std::string run_id = generator ? generator->generate() : "";

    // 更新到状态对象
    m_status.setServerName("Astra-CacheServer")
            .setVersion(ASTRA_VERSION)
            .setVersionSha1(ASTRA_VERSION_SHA1)
            .setBuildId(ASTRA_BUILD_ID)
            .setMode(ASTRA_BUILD_MODE)
            .setOs(ASTRA_OS)
            .setArchBits(ASTRA_ARCH_BITS)
            .setCompilerVersion(ASTRA_COMPILER)
            .setProcessId(process_id)
            .setMode("standalone")
            .setConfigFile("")
            .setRunId(run_id)
            .setTcpPort(std::to_string(current_port))
            .setExecutable(executablePath);
}

// 收集动态状态信息（定期更新）
void StatusCollector::collectDynamicStatus() {
    // 1. 更新运行时间
    auto now = steady_clock::now();
    auto uptime_seconds = duration_cast<seconds>(now - server_start_time).count();
    m_status.setUptimeInSeconds(uptime_seconds);
    m_status.setUptimeInDays(uptime_seconds / 86400);

    // 2. 其他动态指标
    collectMemories();// 内存信息
    collectStats();   // 连接和命令统计
    collectCpu();     // CPU使用率
    collectClients(); // 客户端连接数
}

// 辅助函数：格式化内存大小为人类可读格式
static std::string formatMemorySize(uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unit_idx < 4) {
        size /= 1024;
        unit_idx++;
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << units[unit_idx];
    return ss.str();
}

#ifdef _WIN32

// Windows 平台：收集CPU使用率
void StatusCollector::collectCpu() {
    std::lock_guard<std::mutex> lock(proc_mutex);

    // 1. 获取当前进程CPU时间
    uint64_t proc_sys, proc_user, proc_sys_children, proc_user_children;
    if (!get_process_cpu_times(proc_sys, proc_user, proc_sys_children, proc_user_children)) {
        return;
    }

    // 2. 获取当前系统CPU时间
    uint64_t system_idle, system_kernel, system_user;
    if (!get_system_cpu_times(system_idle, system_kernel, system_user)) {
        return;
    }

    // 3. 计算时间间隔（首次收集时初始化基准）
    auto now = steady_clock::now();
    double interval = duration<double>(now - last_collect_time).count();
    if (interval <= 0 || last_collect_time.time_since_epoch().count() == 0) {
        last_collect_time = now;
        last_proc_sys = proc_sys;
        last_proc_user = proc_user;
        last_proc_sys_children = proc_sys_children;
        last_proc_user_children = proc_user_children;
        last_system_idle = system_idle;
        last_system_kernel = system_kernel;
        last_system_user = system_user;
        return;
    }

    // 4. 计算系统活跃时间（总时间 - 空闲时间）
    uint64_t system_total = (system_kernel - last_system_kernel) + (system_user - last_system_user);
    uint64_t system_active = system_total - (system_idle - last_system_idle);
    if (system_active == 0) {
        m_status.setUsedCpuSys(0.0)
                .setUsedCpuUser(0.0)
                .setUsedCpuSysChildren(0.0)
                .setUsedCpuUserChildren(0.0);
        return;
    }

    // 5. 计算进程CPU使用率（相对系统活跃时间的比例）
    auto calc_usage = [&](uint64_t current, uint64_t last) {
        uint64_t diff = current - last;
        return std::min(100.0, std::max(0.0, static_cast<double>(diff) / system_active * 100));
    };

    // 6. 更新状态
    m_status.setUsedCpuSys(calc_usage(proc_sys, last_proc_sys))
            .setUsedCpuUser(calc_usage(proc_user, last_proc_user))
            .setUsedCpuSysChildren(calc_usage(proc_sys_children, last_proc_sys_children))
            .setUsedCpuUserChildren(calc_usage(proc_user_children, last_proc_user_children));

    // 7. 更新基准值
    last_collect_time = now;
    last_proc_sys = proc_sys;
    last_proc_user = proc_user;
    last_proc_sys_children = proc_sys_children;
    last_proc_user_children = proc_user_children;
    last_system_idle = system_idle;
    last_system_kernel = system_kernel;
    last_system_user = system_user;
}

// Windows 平台：收集内存信息
void StatusCollector::collectMemories() {
    std::lock_guard<std::mutex> lock(proc_mutex);

    uint64_t rss, vsize;
    if (!get_process_memory(rss, vsize)) {
        return;
    }

    // 更新内存状态（used_memory=虚拟内存，used_memory_rss=物理内存）
    m_status.setUsedMemory(vsize)
            .setUsedMemoryRss(rss)
            .setUsedMemoryHuman(formatMemorySize(vsize))
            .setUsedMemoryRssHuman(formatMemorySize(rss));
}

#else

// Linux 平台：收集CPU使用率
void StatusCollector::collectCpu() {
    std::lock_guard<std::mutex> lock(proc_mutex);

    // 1. 获取当前进程CPU时间
    uint64_t proc_sys, proc_user, proc_sys_children, proc_user_children;
    if (!get_process_cpu_times(proc_sys, proc_user, proc_sys_children, proc_user_children)) {
        return;
    }

    // 2. 计算时间间隔
    auto now = steady_clock::now();
    double interval = duration<double>(now - last_collect_time).count();
    if (interval <= 0 || last_collect_time.time_since_epoch().count() == 0) {
        last_collect_time = now;
        last_proc_sys = proc_sys;
        last_proc_user = proc_user;
        last_proc_sys_children = proc_sys_children;
        last_proc_user_children = proc_user_children;
        return;
    }

    // 3. 转换时间单位（100纳秒 -> 秒）
    const double nano_to_sec = 1e-7;// 100纳秒 = 1e-7秒

    // 4. 计算CPU使用率（时间差 / 间隔时间）
    auto calc_usage = [&](uint64_t current, uint64_t last) {
        double diff = (current - last) * nano_to_sec;
        double usage = diff / interval * 100;
        return std::min(100.0, std::max(0.0, usage));
    };

    // 5. 更新状态
    m_status.setUsedCpuSys(calc_usage(proc_sys, last_proc_sys))
            .setUsedCpuUser(calc_usage(proc_user, last_proc_user))
            .setUsedCpuSysChildren(calc_usage(proc_sys_children, last_proc_sys_children))
            .setUsedCpuUserChildren(calc_usage(proc_user_children, last_proc_user_children));

    // 6. 更新基准
    last_collect_time = now;
    last_proc_sys = proc_sys;
    last_proc_user = proc_user;
    last_proc_sys_children = proc_sys_children;
    last_proc_user_children = proc_user_children;
}

// Linux 平台：收集内存信息
void StatusCollector::collectMemories() {
    std::lock_guard<std::mutex> lock(proc_mutex);

    uint64_t rss, vsize;
    if (!get_process_memory(rss, vsize)) {
        return;
    }

    // 更新内存状态（used_memory=虚拟内存，used_memory_rss=物理内存）
    m_status.setUsedMemory(vsize)
            .setUsedMemoryRss(rss)
            .setUsedMemoryHuman(formatMemorySize(vsize))
            .setUsedMemoryRssHuman(formatMemorySize(rss));
}

#endif