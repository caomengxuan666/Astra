#include "WindowsTrayPlugin.h"
#include "core/astra.hpp"
#include <string>
#include <tchar.h>

// 启动独立的系统托盘进程
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // 创建托盘插件实例
    WindowsTrayPlugin trayPlugin(hInstance,
                                 L"IndependentTrayClass",
                                 L"独立系统托盘",
                                 L"我的独立系统托盘进程");

    // 运行托盘插件
    return trayPlugin.Run();
}

// 用于创建独立进程的函数示例
bool StartTrayProcess(const std::wstring &executablePath) {
    STARTUPINFOW si = {0};// 使用宽字符版本的STARTUPINFO
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(STARTUPINFOW);

    // 创建独立进程，使用宽字符版本CreateProcessW
    BOOL success = CreateProcessW(
            executablePath.c_str(),// 应用程序路径（宽字符）
            NULL,                  // 命令行参数
            NULL,                  // 进程安全属性
            NULL,                  // 线程安全属性
            FALSE,                 // 继承句柄
            0,                     // 创建标志
            NULL,                  // 环境变量
            NULL,                  // 当前目录
            &si,                   // 启动信息（宽字符版本）
            &pi                    // 进程信息
    );

    if (success) {
        // 关闭进程和线程句柄
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }

    return false;
}
