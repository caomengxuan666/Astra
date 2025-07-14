#pragma once
#include <string>
#include <vector>

// 兼容 MSVC 和其他系统
#ifdef _MSC_VER
#include <cstring>// for _stricmp in MSVC
#else
#include <strings.h>// for strcasecmp in POSIX systems
#endif

namespace Astra::proto {

    // 判断是否是子命令（不区分大小写）
    inline bool IsSubCommand(const std::vector<std::string> &argv, const std::string &subCmd) {
        if (argv.size() < 2) return false;

#ifdef _MSC_VER
        return _stricmp(argv[1].c_str(), subCmd.c_str()) == 0;
#else
        return strcasecmp(argv[1].c_str(), subCmd.c_str()) == 0;
#endif
    }

    // 另一种实现：通用的大小写不敏感比较
    inline bool ICaseCmp(const std::string &s1, const std::string &s2) {
        if (s1.size() != s2.size()) return false;
        for (size_t i = 0; i < s1.size(); ++i) {
            if (std::toupper(static_cast<unsigned char>(s1[i])) !=
                std::toupper(static_cast<unsigned char>(s2[i]))) {
                return false;
            }
        }
        return true;
    }

}// namespace Astra::proto