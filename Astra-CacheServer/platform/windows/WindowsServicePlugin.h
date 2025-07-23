#pragma once
#include <string>
#include <vector>

// Windows平台声明插件类
#ifdef _WIN32

namespace Astra::apps {
    class AstraCacheServer;
}
class WindowsServicePlugin {
public:
    // 服务控制接口
    bool install(const std::string &exePath, const std::vector<std::string> &args);
    bool uninstall();
    bool start();
    bool stop();

    // 自启动管理接口
    bool setAutoStart(bool enable);

    // 服务模式入口
    void runService();

    // 检查是否为服务模式启动
    static bool isServiceMode(int argc, char *argv[]);
};
// 全局服务器实例指针

extern std::shared_ptr<Astra::apps::AstraCacheServer> g_server;
#endif// _WIN32