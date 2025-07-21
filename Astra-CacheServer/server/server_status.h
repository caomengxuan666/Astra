#pragma once
#include "network/Singleton.h"
#include <cstdio>
#include <string>// 用std::string替代char*，减少内存管理风险

namespace Astra::apps {
    using cStr = const char *;

    // 状态结构体：成员改为非static，属于单例实例的一部分
    struct ServerStatus {
        // server
        std::string server_name;
        std::string version;
        std::string version_sha1;
        std::string build_id;
        std::string mode;
        std::string os;
        std::string arch_bits;
        std::string process_id;
        std::string Compiler_version;
        std::string run_id;
        std::string tcp_port;
        std::string executable;
        std::string config_file;
        size_t uptime_in_seconds = 0;
        size_t uptime_in_days = 0;// 用值类型替代指针，避免内存管理

        // client
        size_t connected_clients = 0;// 用数值类型存储，转换时再转字符串

        // memory
        size_t used_memory = 0;
        std::string used_memory_human;
        size_t used_memory_rss = 0;
        std::string used_memory_rss_human;

        //stats
        size_t total_connections_received = 0;
        size_t total_commands_processed = 0;

        //CPU
        float used_cpu_sys = 0;
        float used_cpu_user = 0;
        float used_cpu_sys_children = 0;
        float used_cpu_user_children = 0;

        static cStr toCsr(size_t integer) {
            thread_local char buffer[32];// 每个线程一个缓冲区
            int ret = snprintf(buffer, sizeof(buffer), "%zu", integer);
            if (ret < 0 || ret >= static_cast<int>(sizeof(buffer))) {
                return "conversion_error";
            }
            return buffer;
        }

        static cStr toCsr(float value) {
            thread_local char buffer[32];
            int ret = snprintf(buffer, sizeof(buffer), "%.2f", value);
            if (ret < 0 || ret >= static_cast<int>(sizeof(buffer))) {
                return "conversion_error";
            }
            return buffer;
        }
    };

    // 单例类：提供public接口访问状态
    class ServerStatusInstance : public Singleton<ServerStatusInstance> {
    public:
        // 提供public接口获取状态（只读/可写根据需求调整）
        const ServerStatus &getStatus() const { return status_; }
        ServerStatus &getStatus() { return status_; }

    private:
        ServerStatusInstance() = default;            // 私有构造，确保单例
        friend class Singleton<ServerStatusInstance>;// 允许基类访问构造函数

        ServerStatus status_;// 非static，属于单例实例的状态
    };
}// namespace Astra::apps
