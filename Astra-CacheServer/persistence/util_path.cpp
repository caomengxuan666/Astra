#include "util_path.hpp"
#include "core/astra.hpp"
#include <filesystem>
#include <utils/logger.hpp>
namespace fs = std::filesystem;
#ifdef _WIN32
#include <windows.h>
#define PATH_MAX MAX_PATH
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace Astra::utils {

    std::string getExecutableDirectory() noexcept {
        std::string path;

#ifdef _WIN32
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        path = buffer;
#else
        char buffer[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1) {
            buffer[len] = '\0';
            path = buffer;
        }
#endif

        size_t last_sep = path.find_last_of("\\/");
        if (last_sep != std::string::npos) {
            return path.substr(0, last_sep);
        }
        return "";
    }

    bool ensureDirectoryExists(const std::string &filepath) {
        namespace fs = std::filesystem;
        try {
            fs::path path(filepath);
            if (path.has_parent_path()) {
                fs::create_directories(path.parent_path());
            }
            return true;
        } catch (const fs::filesystem_error &e) {
            ZEN_LOG_ERROR("Failed to create directory for {}: {}", filepath, e.what());
            return false;
        }
    }
    const char *getEnv() {
        // 获取用户主目录，跨平台实现
        fs::path homeDir;
        const char *homeEnv = nullptr;

#ifdef _WIN32
        homeEnv = std::getenv("USERPROFILE");// Windows系统
#else
        homeEnv = std::getenv("HOME");// Unix/Linux系统
#endif
        return homeEnv;
    }

}// namespace Astra::utils