// command_info.hpp
#pragma once
#include <string>
#include <vector>

namespace Astra::proto {

    struct CommandInfo {
        std::string name;
        int arity;
        std::vector<std::string> flags;
        int first_key;
        int last_key;
        int key_step;
        int64_t microseconds;
        std::string category;
    };

}// namespace Astra::proto