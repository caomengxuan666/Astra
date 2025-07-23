#include "process.hpp"

#ifdef _WIN32
// Windows平台实现
#include <psapi.h>
#include <windows.h>
#pragma comment(lib, "psapi.lib")

namespace Astra::persistence {

    std::string get_pid_str() {
        return std::to_string(GetCurrentProcessId());
    }

    bool get_process_cpu_times(uint64_t &sys_time,
                               uint64_t &user_time,
                               uint64_t &sys_children,
                               uint64_t &user_children) {
        FILETIME creation_time, exit_time, kernel_time, user_time_ft;
        if (!GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time_ft)) {
            return false;
        }

        // FILETIME转64位整数（单位：100纳秒）
        auto ft_to_uint64 = [](const FILETIME &ft) {
            return static_cast<uint64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
        };

        sys_time = ft_to_uint64(kernel_time);
        user_time = ft_to_uint64(user_time_ft);
        sys_children = 0;// 暂不支持子进程时间统计
        user_children = 0;
        return true;
    }

    bool get_system_cpu_times(uint64_t &idle_time,
                              uint64_t &kernel_time,
                              uint64_t &user_time) {
        FILETIME idle_time_ft, kernel_time_ft, user_time_ft;
        if (!GetSystemTimes(&idle_time_ft, &kernel_time_ft, &user_time_ft)) {
            return false;
        }

        // FILETIME转64位整数（单位：100纳秒）
        auto ft_to_uint64 = [](const FILETIME &ft) {
            return static_cast<uint64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
        };

        idle_time = ft_to_uint64(idle_time_ft);
        kernel_time = ft_to_uint64(kernel_time_ft);
        user_time = ft_to_uint64(user_time_ft);
        return true;
    }

    bool get_process_memory(uint64_t &rss,
                            uint64_t &vsize) {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        pmc.cb = sizeof(pmc);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
            return false;
        }

        rss = pmc.WorkingSetSize;// 物理内存（字节）
        vsize = pmc.PrivateUsage;// 虚拟内存（字节）
        return true;
    }

}// namespace Astra::persistence

#else
// Linux平台实现
#include <fstream>
#include <sys/sysinfo.h>
#include <unistd.h>

namespace Astra::persistence {

    std::string get_pid_str() {
        return std::to_string(getpid());
    }

    bool get_process_cpu_times(uint64_t &sys_time,
                               uint64_t &user_time,
                               uint64_t &sys_children,
                               uint64_t &user_children) {
        std::ifstream stat_file("/proc/" + std::to_string(getpid()) + "/stat");
        if (!stat_file.is_open()) {
            return false;
        }

        std::string line;
        std::getline(stat_file, line);
        std::istringstream iss(line);

        // /proc/[pid]/stat格式：14=utime(用户态), 15=stime(内核态), 16=cutime(子进程用户态), 17=cstime(子进程内核态)
        std::string token;
        int field = 0;
        uint64_t utime = 0, stime = 0, cutime = 0, cstime = 0;

        while (iss >> token) {
            field++;
            switch (field) {
                case 14:
                    utime = std::stoull(token);
                    break;
                case 15:
                    stime = std::stoull(token);
                    break;
                case 16:
                    cutime = std::stoull(token);
                    break;
                case 17:
                    cstime = std::stoull(token);
                    break;
            }
        }

        // 转换为100纳秒单位（Linux时钟周期 = 1/clk_tck 秒）
        double clk_tck = sysconf(_SC_CLK_TCK);
        const double nano_convert = 10000000.0 / clk_tck;// 转换系数

        sys_time = static_cast<uint64_t>(stime * nano_convert);
        user_time = static_cast<uint64_t>(utime * nano_convert);
        sys_children = static_cast<uint64_t>(cstime * nano_convert);
        user_children = static_cast<uint64_t>(cutime * nano_convert);
        return true;
    }

    bool get_system_cpu_times(uint64_t &idle_time,
                              uint64_t &kernel_time,
                              uint64_t &user_time) {
        std::ifstream stat_file("/proc/stat");
        if (!stat_file.is_open()) {
            return false;
        }

        std::string line;
        std::getline(stat_file, line);// 首行是CPU汇总数据
        std::istringstream iss(line);

        // /proc/stat格式：cpu  user(用户态) nice(低优先级用户态) system(内核态) idle(空闲) iowait(IO等待) irq(硬中断) softirq(软中断)
        std::string cpu;
        uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
        iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;

        // 转换为100纳秒单位
        double clk_tck = sysconf(_SC_CLK_TCK);
        const double nano_convert = 10000000.0 / clk_tck;

        idle_time = static_cast<uint64_t>((idle + iowait) * nano_convert);           // 空闲时间（含IO等待）
        kernel_time = static_cast<uint64_t>((system + irq + softirq) * nano_convert);// 内核态时间
        user_time = static_cast<uint64_t>((user + nice) * nano_convert);             // 用户态时间
        return true;
    }

    bool get_process_memory(uint64_t &rss,
                            uint64_t &vsize) {
        std::ifstream statm_file("/proc/" + std::to_string(getpid()) + "/statm");
        if (!statm_file.is_open()) {
            return false;
        }

        // /proc/[pid]/statm格式：size(总页数) resident(常驻页数) ...
        uint64_t size_pages, resident_pages;
        statm_file >> size_pages >> resident_pages;

        // 转换为字节（页大小 = sysconf(_SC_PAGESIZE)）
        uint64_t page_size = sysconf(_SC_PAGESIZE);
        vsize = size_pages * page_size;  // 虚拟内存
        rss = resident_pages * page_size;// 物理内存
        return true;
    }

}// namespace Astra::persistence

#endif