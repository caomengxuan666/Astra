#include <iostream>
#include <network/Singleton.h>

static std::string formatMemorySize(uint64_t bytes);
namespace Astra::apps {
    using cStr = const char *;

    struct ServerStatus {
        //server 相关字段
        std::string server_name;     //static
        std::string version;         //static
        std::string version_sha1;    //static
        std::string build_id;        //static
        std::string mode;            //static
        std::string os;              //static
        std::string arch_bits;       //static
        std::string process_id;      //dynamic
        std::string Compiler_version;//static
        std::string run_id;          //dynamic
        std::string tcp_port;        //dynamic
        std::string executable;      //dynamic
        std::string config_file;     //dynamic
        size_t uptime_in_seconds = 0;//dynamic
        size_t uptime_in_days = 0;   //dynamic

        //client 相关字段
        size_t connected_clients = 0;//dynamic

        //memory 相关字段
        size_t used_memory = 0;           //dynamic
        std::string used_memory_human;    //dynamic
        size_t used_memory_rss = 0;       //dynamic
        std::string used_memory_rss_human;//dynamic

        //stats 相关字段
        size_t total_connections_received = 0;//dynamic
        size_t total_commands_processed = 0;  //dynamic

        // CPU 相关字段
        float used_cpu_sys = 0;          //dynamic
        float used_cpu_user = 0;         //dynamic
        float used_cpu_sys_children = 0; //dynamic
        float used_cpu_user_children = 0;//dtbamic

        //类型转换成员函数
        cStr toCsr(const std::string &str) const {
            return str.c_str();// std::string 本身可直接转为 const char*
        }

        cStr toCsr(size_t integer) const {
            thread_local char buffer[32];// 线程局部缓冲区，避免竞争
            int ret = snprintf(buffer, sizeof(buffer), "%zu", integer);
            return (ret >= 0 && ret < static_cast<int>(sizeof(buffer))) ? buffer : "conversion_error";
        }

        cStr toCsr(int integer) const {
            thread_local char buffer[32];
            int ret = snprintf(buffer, sizeof(buffer), "%d", integer);// 使用 %d 格式化
            return (ret >= 0 && ret < static_cast<int>(sizeof(buffer))) ? buffer : "conversion_error";
        }

        cStr toCsr(float value) const {
            thread_local char buffer[32];
            int ret = snprintf(buffer, sizeof(buffer), "%.2f", value);// 控制精度
            return (ret >= 0 && ret < static_cast<int>(sizeof(buffer))) ? buffer : "conversion_error";
        }

        cStr toCsrServerName() const { return toCsr(server_name); }
        cStr toCsrVersion() const { return toCsr(version); }


        //------------------------------ 链式调用 setter 函数 ------------------------------
        ServerStatus &setServerName(const std::string &val) {
            server_name = val;
            return *this;
        }
        ServerStatus &setVersion(const std::string &val) {
            version = val;
            return *this;
        }
        ServerStatus &setVersionSha1(const std::string &val) {
            version_sha1 = val;
            return *this;
        }
        ServerStatus &setBuildId(const std::string &val) {
            build_id = val;
            return *this;
        }
        ServerStatus &setMode(const std::string &val) {
            mode = val;
            return *this;
        }
        ServerStatus &setOs(const std::string &val) {
            os = val;
            return *this;
        }
        ServerStatus &setArchBits(const std::string &val) {
            arch_bits = val;
            return *this;
        }
        ServerStatus &setProcessId(const std::string &val) {
            process_id = val;
            return *this;
        }
        ServerStatus &setCompilerVersion(const std::string &val) {
            Compiler_version = val;
            return *this;
        }
        ServerStatus &setRunId(const std::string &val) {
            run_id = val;
            return *this;
        }
        ServerStatus &setTcpPort(const std::string &val) {
            tcp_port = val;
            return *this;
        }
        ServerStatus &setExecutable(const std::string &val) {
            executable = val;
            return *this;
        }
        ServerStatus &setConfigFile(const std::string &val) {
            config_file = val;
            return *this;
        }
        ServerStatus &setUptimeInSeconds(size_t val) {
            uptime_in_seconds = val;
            return *this;
        }
        ServerStatus &setUptimeInDays(size_t val) {
            uptime_in_days = val;
            return *this;
        }
        ServerStatus &setConnectedClients(size_t val) {
            connected_clients = val;
            return *this;
        }
        ServerStatus &setUsedMemory(size_t val) {
            used_memory = val;
            return *this;
        }
        ServerStatus &setUsedMemoryHuman(const std::string &val) {
            used_memory_human = val;
            return *this;
        }
        ServerStatus &setUsedMemoryRss(size_t val) {
            used_memory_rss = val;
            return *this;
        }
        ServerStatus &setUsedMemoryRssHuman(const std::string &val) {
            used_memory_rss_human = val;
            return *this;
        }
        ServerStatus &setTotalConnectionsReceived(size_t val) {
            total_connections_received = val;
            return *this;
        }
        ServerStatus &setTotalCommandsProcessed(size_t val) {
            total_commands_processed = val;
            return *this;
        }
        ServerStatus &setUsedCpuSys(float val) {
            used_cpu_sys = val;
            return *this;
        }
        ServerStatus &setUsedCpuUser(float val) {
            used_cpu_user = val;
            return *this;
        }
        ServerStatus &setUsedCpuSysChildren(float val) {
            used_cpu_sys_children = val;
            return *this;
        }
        ServerStatus &setUsedCpuUserChildren(float val) {
            used_cpu_user_children = val;
            return *this;
        }
    };

    class ServerStatusInstance : public Singleton<ServerStatusInstance> {
    public:
        const ServerStatus &getStatus() const { return status_; }
        ServerStatus &getStatus() { return status_; }

    private:
        ServerStatusInstance() = default;
        friend class Singleton<ServerStatusInstance>;

        ServerStatus status_;
    };
}// namespace Astra::apps
