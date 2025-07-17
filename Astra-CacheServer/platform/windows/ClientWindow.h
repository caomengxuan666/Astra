
// 客户端窗口类声明
#pragma once
#include <string>
#include <windows.h>

class ClientWindow {
public:
    // 构造函数
    ClientWindow(HINSTANCE hInstance,
                 const wchar_t *className,
                 const wchar_t *windowTitle);

    // 析构函数
    ~ClientWindow();

    // 显示或激活窗口
    void Show();

    // 检查窗口是否已创建
    bool IsCreated() const { return m_hWnd != nullptr; }

    // 获取窗口句柄
    HWND GetWindowHandle() const { return m_hWnd; }

private:
    HINSTANCE m_hInstance;
    HWND m_hWnd;
    std::wstring m_className;
    std::wstring m_windowTitle;

    // 窗口过程
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 处理窗口消息
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 注册窗口类
    bool RegisterWindowClass();

    // 创建窗口
    void Create();
};