#pragma once
#include <string>
#include <vector>

namespace Astra::proto {
    // 抽象命令接口
    class ICommand {
    public:
        virtual ~ICommand() = default;
        virtual std::string Execute(const std::vector<std::string> &argv) = 0;
    };

}// namespace Astra::proto