#include "ClientWindow.h"

// 定义宽字符版本的箭头光标资源ID
#ifndef IDC_ARROWW
#define IDC_ARROWW MAKEINTRESOURCEW(32512)
#endif

// 客户端窗口实现
ClientWindow::ClientWindow(HINSTANCE hInstance,
                         const wchar_t* className,
                         const wchar_t* windowTitle)
    : m_hInstance(hInstance),
      m_className(className),
      m_windowTitle(windowTitle),
      m_hWnd(nullptr) {
}

ClientWindow::~ClientWindow() {
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

void ClientWindow::Show() {
    if (!IsCreated()) {
        Create();
    }

    if (IsCreated()) {
        if (IsIconic(m_hWnd)) {
            ShowWindow(m_hWnd, SW_RESTORE);
        } else {
            ShowWindow(m_hWnd, SW_SHOW);
        }

        BringWindowToTop(m_hWnd);

        DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
        DWORD currentThreadId = GetCurrentThreadId();
        BOOL attached = FALSE;
        if (foregroundThreadId != currentThreadId) {
            attached = AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
        }
        SetForegroundWindow(m_hWnd);
        if (attached) {
            AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
        }

        UpdateWindow(m_hWnd);
    }
}

bool ClientWindow::RegisterWindowClass() {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = m_className.c_str();
    // 使用显式宽字符版本的IDC_ARROW
    wc.hCursor = LoadCursorW(NULL, IDC_ARROWW);
    // 添加背景画刷，解决透明问题
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    // 使用标准的窗口样式，避免潜在的显示问题
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

    return RegisterClassExW(&wc) != 0;
}

void ClientWindow::Create() {
    if (!RegisterWindowClass()) {
        MessageBoxW(NULL, L"客户端窗口类注册失败！", L"错误", MB_ICONEXCLAMATION | MB_OK);
        return;
    }

    m_hWnd = CreateWindowExW(
        0,
        m_className.c_str(),
        m_windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, m_hInstance, this);

    if (m_hWnd == NULL) {
        MessageBoxW(NULL, L"客户端窗口创建失败！", L"错误", MB_ICONEXCLAMATION | MB_OK);
        return;
    }

    // 关键修改：使用SW_SHOWNORMAL确保首次显示正常
    // 同时强制设置窗口为前台并激活
    ShowWindow(m_hWnd, SW_SHOWNORMAL);
    UpdateWindow(m_hWnd);
    // 强制刷新窗口以确保标题栏正确显示
    RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

    // 额外确保窗口激活的代码
    SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hWnd);
    SetActiveWindow(m_hWnd);
}

LRESULT CALLBACK ClientWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 存储和获取类实例指针
    if (msg == WM_NCCREATE) {
        LPCREATESTRUCTW pCreateStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    } else {
        ClientWindow *pThis = reinterpret_cast<ClientWindow *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (pThis) {
            return pThis->HandleMessage(msg, wParam, lParam);
        }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT ClientWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            // 初始化客户端窗口内容
            return 0;

        case WM_DESTROY:
            // 窗口销毁时不直接置空句柄，由析构函数处理
            return 0;

        default:
            return DefWindowProcW(m_hWnd, msg, wParam, lParam);
    }
}