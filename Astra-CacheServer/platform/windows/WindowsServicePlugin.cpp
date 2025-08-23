#include "core/astra.hpp"
#include "server/status_collector.h"
#ifdef _WIN32
#include "WindowsServicePlugin.h"

#include "network/io_context_pool.hpp"
#include "persistence/util_path.hpp"
#include "server/server.hpp"
#include "server/server_init.h"
#include "utils/logger.hpp"
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <filesystem>
#include <memory>
#include <shellapi.h>


// 定义Unicode宏（使用宽字符版本API）
#ifndef _T
#define _T(x) L##x
#endif

// 服务名称与显示名称定义（使用宽字符）
#define SERVICE_NAME L"AstraCacheService"
#define SERVICE_DISPLAY_NAME L"Astra Cache Server"

// 全局服务器实例指针
static std::shared_ptr<Astra::apps::AstraCacheServer> g_server;

// 服务状态与状态句柄
static SERVICE_STATUS g_serviceStatus;
static SERVICE_STATUS_HANDLE g_serviceStatusHandle;


// 设置服务状态
void SetServiceStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
    static DWORD dwCheckPoint = 1;

    g_serviceStatus.dwCurrentState = dwCurrentState;
    g_serviceStatus.dwWin32ExitCode = dwWin32ExitCode;
    g_serviceStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        g_serviceStatus.dwControlsAccepted = 0;
    else
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED))
        g_serviceStatus.dwCheckPoint = 0;
    else
        g_serviceStatus.dwCheckPoint = dwCheckPoint++;

    ::SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

// 服务控制处理函数
void WINAPI ServiceCtrlHandler(DWORD dwCtrl) {
    switch (dwCtrl) {
        case SERVICE_CONTROL_STOP:
            SetServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);

            // 检查g_server是否有效
            if (!g_server) {
                ZEN_LOG_ERROR("g_server为空，无法停止服务");
                SetServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
                return;
            }

            // 执行停止逻辑
            ZEN_LOG_INFO("开始停止g_server");// 与启动时的地址对比，必须一致
            g_server->Stop();                // 调用服务器的停止方法
            g_server.reset();                // 强制释放实例（关键！避免残留）
            ZEN_LOG_INFO("g_server已释放");

            // 停止IO线程池（确保没有残留线程）
            auto pool = AsioIOServicePool::GetInstance();
            pool->Stop();// 终止所有线程

            SetServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, GetCurrentProcessId());
            TerminateProcess(hProcess, 0);// 强制终止当前服务进程
            return;
    }
}

// 服务模式下启动服务器
// 服务模式下启动服务器
void StartServerInServiceMode(int argc, char *argv[]) {
    // 1. 初始化ConfigManager（服务模式专用）
    auto config_manager = apps::ConfigManager::GetInstance();
    if (!config_manager->initializeForService(argc, argv)) {
        ZEN_LOG_ERROR("服务模式配置初始化失败");
        throw std::runtime_error("Service config init failed");
    }

    // 2. 安全获取配置参数
    uint16_t listening_port = 0;
    size_t max_lru_size = 0;
    std::string persistence_file;
    try {
        listening_port = config_manager->getListeningPort();
        max_lru_size = config_manager->getMaxLRUSize();
        persistence_file = config_manager->getPersistenceFileName();
    } catch (const std::exception &e) {
        ZEN_LOG_ERROR("配置获取失败: {}", e.what());
        listening_port = 6380;// 默认端口
        max_lru_size = std::numeric_limits<size_t>::max();
        persistence_file = "";
    }

    // 3. 初始化日志系统
    try {
        auto &logger = Astra::Logger::GetInstance();
        if (config_manager->getEnableLoggingFile()) {
            auto fileAppender = std::make_shared<Astra::SyncFileAppender>(logger.GetDefaultLogDir());
            logger.AddAppender(fileAppender);
        }
        ZEN_SET_LEVEL(config_manager->getLogLevel());
    } catch (...) {
        ZEN_LOG_ERROR("日志系统初始化失败，继续运行...");
    }

    // 4. 启动状态收集器（安全模式）
    if (auto collector = apps::StatusCollector::GetInstance()) {
        try {
            collector->start();
        } catch (const std::exception &e) {
            ZEN_LOG_ERROR("状态收集器启动失败: {}", e.what());
        }
    }

    // 5. 启动服务器核心
    try {
        auto pool = AsioIOServicePool::GetInstance();
        asio::io_context &io_context = pool->GetIOService();

        g_server = std::make_shared<Astra::apps::AstraCacheServer>(
                io_context,
                max_lru_size,
                persistence_file);

        g_server->setEnablePersistence(false);
        g_server->Start(config_manager->getBindAddress(), listening_port);

        ZEN_LOG_INFO("服务已启动，监听端口: {}", listening_port);
        ZEN_LOG_INFO("LRU最大尺寸: {}", max_lru_size);
        ZEN_LOG_INFO("持久化文件: {}", persistence_file.empty() ? "无" : persistence_file);

    } catch (const std::exception &e) {
        ZEN_LOG_ERROR("服务器启动失败: {}", e.what());
        throw;
    }
}

// 服务主函数
void WINAPI ServiceMain(DWORD, LPWSTR *) {
    g_serviceStatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_serviceStatusHandle) return;

    // 初始化服务状态
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    SetServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    try {
        // 转换命令行参数
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<char *> args;

        for (int i = 0; i < argc; ++i) {
            int len = WideCharToMultiByte(CP_ACP, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            char *buf = new char[len];
            WideCharToMultiByte(CP_ACP, 0, argv[i], -1, buf, len, nullptr, nullptr);
            args.push_back(buf);
        }

        StartServerInServiceMode(argc, args.data());

        // 清理资源
        for (char *arg: args) delete[] arg;
        LocalFree(argv);

        SetServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
    } catch (...) {
        SetServiceStatus(SERVICE_STOPPED, 1, 0);
    }
}

// 安装服务
bool WindowsServicePlugin::install(const std::string &exePath, const std::vector<std::string> &args) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        ZEN_LOG_ERROR("打开服务管理器失败: {}", GetLastError());
        return false;
    }

    // 构建命令行
    std::wstring cmdLine = L"\"" + std::wstring(exePath.begin(), exePath.end()) + L"\" --service";
    for (const auto &arg: args) {
        cmdLine += L" \"" + std::wstring(arg.begin(), arg.end()) + L"\"";
    }

    // 创建服务
    SC_HANDLE service = CreateServiceW(
            scm,
            SERVICE_NAME,
            SERVICE_DISPLAY_NAME,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            cmdLine.c_str(),
            nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!service) {
        ZEN_LOG_ERROR("创建服务失败: {}", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    // 添加服务描述
    std::wstring description = L"Astra Cache Server, a high - performance cache service.";
    description += L" This great service is compatible with Redis protocol and provides fast caching capabilities.";
    description += L" And the most import thing is that CMX is a 大帅比.";
    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = const_cast<wchar_t *>(description.c_str());
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc)) {
        ZEN_LOG_ERROR("设置服务描述失败: %d", GetLastError());
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

// 卸载服务
bool WindowsServicePlugin::uninstall() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, SERVICE_NAME, DELETE);
    if (!service) {
        CloseServiceHandle(scm);
        return false;
    }

    bool success = DeleteService(service);
    if (!success) ZEN_LOG_ERROR("删除服务失败: {}", GetLastError());

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}

// 启动/停止服务
bool WindowsServicePlugin::start() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, SERVICE_NAME, SERVICE_START);
    if (!service) {
        CloseServiceHandle(scm);
        return false;
    }

    bool success = StartServiceW(service, 0, nullptr);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}

bool WindowsServicePlugin::stop() {
    // 1. 查找服务对应的进程ID
    DWORD pid = 0;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE service = OpenServiceW(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
        if (service) {
            SERVICE_STATUS_PROCESS status;
            DWORD bytesNeeded;
            // 查询服务进程ID
            if (QueryServiceStatusEx(
                        service,
                        SC_STATUS_PROCESS_INFO,
                        (LPBYTE) &status,
                        sizeof(status),
                        &bytesNeeded)) {
                pid = status.dwProcessId;// 获取服务进程ID
            }
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }

    // 2. 强制终止服务进程（直接绕过服务控制逻辑）
    if (pid != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess) {
            TerminateProcess(hProcess, 0);// 强制终止
            CloseHandle(hProcess);
            return true;
        }
    }

    ZEN_LOG_ERROR("停止服务失败，进程ID获取失败");
    return false;
}

// 服务模式运行
void WindowsServicePlugin::runService() {
    // 创建可写的宽字符服务名称
    wchar_t serviceName[] = L"AstraCacheService";

    SERVICE_TABLE_ENTRYW serviceTable[2];
    serviceTable[0].lpServiceName = serviceName;
    serviceTable[0].lpServiceProc = ServiceMain;
    serviceTable[1].lpServiceName = nullptr;
    serviceTable[1].lpServiceProc = nullptr;

    StartServiceCtrlDispatcherW(serviceTable);
}

// 检查是否服务模式
bool WindowsServicePlugin::isServiceMode(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--service") return true;
    }
    return false;
}

bool WindowsServicePlugin::setAutoStart(bool enable) {
    // 打开服务管理器
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        ZEN_LOG_ERROR("打开服务管理器失败: {}", GetLastError());
        return false;
    }

    // 打开指定服务（需要SERVICE_CHANGE_CONFIG权限）
    SC_HANDLE service = OpenServiceW(scm, SERVICE_NAME, SERVICE_CHANGE_CONFIG);
    if (!service) {
        ZEN_LOG_ERROR("打开服务失败: {}", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    // 设置启动类型：自动启动(enable=true)或手动启动(enable=false)
    DWORD startType = enable ? SERVICE_AUTO_START : SERVICE_DEMAND_START;
    bool success = ChangeServiceConfigW(
            service,
            SERVICE_NO_CHANGE,// 服务类型不变
            startType,        // 启动类型（自动/手动）
            SERVICE_NO_CHANGE,// 错误控制不变
            nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr);

    if (!success) {
        ZEN_LOG_ERROR("修改服务启动类型失败: {}", GetLastError());
    } else {
        ZEN_LOG_INFO("服务启动类型已设置为: {}", enable ? "自动启动" : "手动启动");
    }

    // 清理资源
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}
#endif