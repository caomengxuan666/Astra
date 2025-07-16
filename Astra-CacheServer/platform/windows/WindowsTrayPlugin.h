#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include "ClientWindow.h"
#include<memory>

#define ID_ABOUT 101
#define ID_EXIT 102
#define ID_OPEN_CLIENT 1003 // 客户端菜单ID

class WindowsTrayPlugin {
public:
    // 构造函数
    WindowsTrayPlugin(HINSTANCE hInstance,
                     const wchar_t* className,
                     const wchar_t* windowTitle,
                     const wchar_t* tooltip);

    // 析构函数
    ~WindowsTrayPlugin();

    // 运行托盘消息循环
    int Run();

    // 设置托盘图标
    void SetIcon(HICON hIcon);

    // 设置提示文本
    void SetTooltip(const wchar_t* tooltip);

    HICON CreateCustomIcon();

private:
    // 常量定义
    static const UINT WM_TRAYICON = WM_USER + 1;

    // 成员变量
    HINSTANCE m_hInstance;
    HWND m_hWnd;
    NOTIFYICONDATAW m_nid;  // 使用NOTIFYICONDATAW结构体类型
    std::wstring m_className;
    std::wstring m_windowTitle;
    std::wstring m_tooltip;


    // 窗口过程
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 实际消息处理
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 创建托盘图标
    void CreateTrayIcon();

    // 显示托盘菜单
    void ShowTrayMenu(POINT pt);

    // 销毁托盘图标
    void DestroyTrayIcon();

    bool RegisterWindowClass();

    // 注册客户端窗口类
    bool RegisterClientWindowClass();

    // 客户端窗口相关
    std::unique_ptr<ClientWindow> m_clientWindow;
    std::wstring m_clientClassName;
    std::wstring m_clientWindowTitle;

    // 创建客户端窗口
    void ShowClientWindow();

    // 客户端窗口消息处理
    static LRESULT CALLBACK ClientWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleClientMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 客户端窗口类名和标题
    HWND m_clientHWnd;

private:
};
