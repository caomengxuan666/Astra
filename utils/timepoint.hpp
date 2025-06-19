/**
 * @FilePath     : /Astra/utils/timepoint.hpp
 * @Description  :  
 * @Author       : caomengxuan666 2507560089@qq.com
 * @Version      : 0.0.1
 * @LastEditors  : caomengxuan666 2507560089@qq.com
 * @LastEditTime : 2025-06-19 16:35:51
 * @Copyright    : PESONAL DEVELOPER CMX., Copyright (c) 2025.
**/
#pragma once
#include <chrono>
#include <string>
namespace Astra {

    // 工具函数：将时间点转为毫秒字符串
    template<typename Clock>
    std::string ToMilliString(const std::chrono::time_point<Clock> &tp) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          tp.time_since_epoch())
                          .count();
        return std::to_string(ms);
    }

}// namespace Astra
