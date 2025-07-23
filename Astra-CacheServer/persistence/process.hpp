#pragma once
#include <cstdint>
#include <string>

namespace Astra::persistence {

    /**
 * @brief 获取当前进程ID的字符串形式
 * @return 进程ID字符串
 */
    std::string get_pid_str();

    /**
 * @brief 获取进程CPU时间（单位：100纳秒，跨平台统一）
 * @param[out] sys_time 内核态时间
 * @param[out] user_time 用户态时间
 * @param[out] sys_children 子进程内核态时间（暂为0）
 * @param[out] user_children 子进程用户态时间（暂为0）
 * @return 成功返回true，失败返回false
 */
    bool get_process_cpu_times(uint64_t &sys_time,
                               uint64_t &user_time,
                               uint64_t &sys_children,
                               uint64_t &user_children);

    /**
 * @brief 获取系统CPU时间（单位：100纳秒，跨平台统一）
 * @param[out] idle_time 系统空闲时间
 * @param[out] kernel_time 系统内核态总时间
 * @param[out] user_time 系统用户态总时间
 * @return 成功返回true，失败返回false
 */
    bool get_system_cpu_times(uint64_t &idle_time,
                              uint64_t &kernel_time,
                              uint64_t &user_time);

    /**
 * @brief 获取进程内存信息
 * @param[out] rss 物理内存（常驻集大小，字节）
 * @param[out] vsize 虚拟内存（字节）
 * @return 成功返回true，失败返回false
 */
    bool get_process_memory(uint64_t &rss,
                            uint64_t &vsize);

}// namespace Astra::persistence