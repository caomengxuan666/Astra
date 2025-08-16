#include "process.hpp"
#include <sstream>

#if defined(__APPLE__)
// macOS平台实现
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>

namespace Astra::persistence {

    std::string get_pid_str() {
        return std::to_string(getpid());
    }

    bool get_process_cpu_times(uint64_t &sys_time,
                               uint64_t &user_time,
                               uint64_t &sys_children,
                               uint64_t &user_children) {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) != 0) {
            return false;
        }

        // 转换为100纳秒单位（1微秒 = 10 个100纳秒单位）
        const uint64_t microsec_to_100nanosec = 10;

        sys_time = usage.ru_stime.tv_sec * 10000000 + usage.ru_stime.tv_usec * microsec_to_100nanosec;
        user_time = usage.ru_utime.tv_sec * 10000000 + usage.ru_utime.tv_usec * microsec_to_100nanosec;

        // 子进程时间
        sys_children = usage.ru_stimesv.tv_sec * 10000000 + usage.ru_stimesv.tv_usec * microsec_to_100nanosec;
        user_children = usage.ru_utimesv.tv_sec * 10000000 + usage.ru_utimesv.tv_usec * microsec_to_100nanosec;

        return true;
    }

    bool get_system_cpu_times(uint64_t &idle_time,
                              uint64_t &kernel_time,
                              uint64_t &user_time) {
        // 使用host_processor_info获取CPU负载信息
        processor_info_array_t cpu_info;
        mach_msg_type_number_t msg_type;
        natural_t processor_count;
        kern_return_t kr = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &processor_count, &cpu_info, &msg_type);
        if (kr != KERN_SUCCESS) {
            return false;
        }

        processor_cpu_load_info_t cpu_load_info = (processor_cpu_load_info_t) cpu_info;

        // 累加所有处理器核心的负载信息
        uint64_t user = 0, system = 0, idle = 0, nice = 0;
        for (natural_t i = 0; i < processor_count; i++) {
            user += cpu_load_info[i].cpu_ticks[CPU_STATE_USER];
            system += cpu_load_info[i].cpu_ticks[CPU_STATE_SYSTEM];
            idle += cpu_load_info[i].cpu_ticks[CPU_STATE_IDLE];
            nice += cpu_load_info[i].cpu_ticks[CPU_STATE_NICE];
        }

        // 释放内存
        vm_deallocate(mach_task_self(), (vm_address_t) cpu_info, msg_type);

        // macOS上的CPU时间单位是ticks，需要转换为100纳秒
        // 假设时钟频率为HZ=100 ticks/second，则1 tick = 10,000,000 纳秒 = 100,000 个100纳秒单位
        const uint64_t tick_to_100nanosec = 100000;

        idle_time = idle * tick_to_100nanosec;
        kernel_time = system * tick_to_100nanosec;
        user_time = (user + nice) * tick_to_100nanosec;

        return true;
    }

    bool get_process_memory(uint64_t &rss,
                            uint64_t &vsize) {
        task_t task = mach_task_self();
        struct task_basic_info info;
        mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;

        if (task_info(task, TASK_BASIC_INFO, (task_info_t) &info, &count) != KERN_SUCCESS) {
            return false;
        }

        rss = info.resident_size; // 物理内存（字节）
        vsize = info.virtual_size;// 虚拟内存（字节）
        return true;
    }

}// namespace Astra::persistence

#elif defined(_WIN32)
// Windows平台实现
#include <windows.h>
// add a comment to not let the psapi.h be formatted before the windows.h
#include <psapi.h>

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