#include "core/astra.hpp"
#include "server/server_init.h"

int main(int argc, char *argv[]) {

    // ********** 优先处理服务控制命令（重构后）**********
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 先判断是否是服务控制命令（install/start/stop等）
    if (argc >= 2) {
        // 尝试处理服务控制命令
        if (handleServiceCommand(argc, argv)) {
            return 0;
        }
    }
#endif

    // ********** 服务命令处理完毕后，再解析普通参数 **********
    // 检查是否为服务模式启动
#ifdef _WIN32
    WindowsServicePlugin winPlugin;
    if (winPlugin.isServiceMode(argc, argv)) {
        winPlugin.runService();

        return 0;
    }
#endif

    // 通用服务器启动逻辑
    return startServer(argc, argv);
}