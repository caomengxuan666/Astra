/**
 * @FilePath     : /Astra/Astra-CacheServer/proto/command_parser.hpp
 * @Description  :  
 * @Author       : caomengxuan666 2507560089@qq.com
 * @Version      : 0.0.1
 * @LastEditors  : caomengxuan666 2507560089@qq.com
 * @LastEditTime : 2025-05-28 20:56:58
 * @Copyright    : PESONAL DEVELOPER CMX., Copyright (c) 2025.
**/
#pragma once
#include <string>
#include <strings.h>
#include <vector>

namespace Astra::proto {
    // 判断是否是子命令
    inline bool IsSubCommand(const std::vector<std::string> &argv, const std::string &subCmd) {
        if (argv.size() < 2) return false;
        return strcasecmp(argv[1].c_str(), subCmd.c_str()) == 0;
    }

    inline bool ICaseCmp(const std::string &s1, const std::string &s2) {
        if (s1.size() != s2.size()) return false;
        for (size_t i = 0; i < s1.size(); ++i) {
            if (std::toupper(s1[i]) != std::toupper(s2[i])) return false;
        }
        return true;
    }

}// namespace Astra::proto