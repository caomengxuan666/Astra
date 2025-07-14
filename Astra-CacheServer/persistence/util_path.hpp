#pragma once
#include <string>

namespace Astra::utils {
    std::string getExecutableDirectory() noexcept;
    bool ensureDirectoryExists(const std::string &filepath);
    const char *getEnv();
}// namespace Astra::utils
