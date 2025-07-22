#include "WindowsTrayPlugin.h"
#include "core/astra.hpp"

#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Gdi32.lib")

#ifndef IDC_ARROWW
#define IDC_ARROWW MAKEINTRESOURCEW(32512)
#endif

// 注册客户端窗口类
bool WindowsTrayPlugin::RegisterClientWindowClass() {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = ClientWndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = m_clientClassName.c_str();
    // 修正：使用IDC_ARROWW宏代替原来的IDC_ARROW
    wc.hCursor = LoadCursorW(NULL, IDC_ARROWW);
    // 添加背景画刷，解决透明问题
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    // 添加窗口样式，确保边框正常显示
    wc.style = CS_HREDRAW | CS_VREDRAW;
    // 使用标准的窗口样式，避免潜在的显示问题
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    // 替换旧的LoadCursorW调用，确保使用宽字符资源标识符
    wc.hCursor = LoadCursorW(NULL, IDC_ARROWW);

    // 使用显式宽字符版本的注册函数
    return RegisterClassExW(&wc) != 0;
}

// 创建客户端窗口
void WindowsTrayPlugin::ShowClientWindow() {
    if (!m_clientHWnd) {
        if (!RegisterClientWindowClass()) {
            MessageBoxW(NULL, L"客户端窗口类注册失败！", L"错误", MB_ICONEXCLAMATION | MB_OK);
            return;
        }

        m_clientHWnd = CreateWindowExW(
                0,
                m_clientClassName.c_str(),
                m_clientWindowTitle.c_str(),
                WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,// 添加WS_VISIBLE样式
                CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                NULL, NULL, m_hInstance, this);

        if (m_clientHWnd == NULL) {
            MessageBoxW(NULL, L"客户端窗口创建失败！", L"错误", MB_ICONEXCLAMATION | MB_OK);
            return;
        }

        // 关键修改：使用SW_SHOWNORMAL确保首次显示正常
        // 同时强制设置窗口为前台并激活
        ShowWindow(m_clientHWnd, SW_SHOWNORMAL);
        UpdateWindow(m_clientHWnd);
        // 强制刷新窗口以确保标题栏正确显示
        RedrawWindow(m_clientHWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

        // 额外确保窗口激活的代码
        SetWindowPos(m_clientHWnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(m_clientHWnd);
        SetActiveWindow(m_clientHWnd);
    } else {
        if (IsIconic(m_clientHWnd)) {
            ShowWindow(m_clientHWnd, SW_RESTORE);
        } else {
            // 确保已显示的窗口被激活
            ShowWindow(m_clientHWnd, SW_SHOW);
        }

        BringWindowToTop(m_clientHWnd);

        DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
        DWORD currentThreadId = GetCurrentThreadId();
        BOOL attached = FALSE;
        if (foregroundThreadId != currentThreadId) {
            attached = AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
        }
        SetForegroundWindow(m_clientHWnd);
        if (attached) {
            AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
        }

        UpdateWindow(m_clientHWnd);
    }
}

// 客户端窗口过程
LRESULT CALLBACK WindowsTrayPlugin::ClientWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 存储和获取类实例指针
    if (msg == WM_NCCREATE) {
        LPCREATESTRUCTW pCreateStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    } else {
        WindowsTrayPlugin *pThis = reinterpret_cast<WindowsTrayPlugin *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (pThis) {
            return pThis->HandleClientMessage(msg, wParam, lParam);
        }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 处理客户端窗口消息
LRESULT WindowsTrayPlugin::HandleClientMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            // 初始化客户端窗口内容
            return 0;

        case WM_DESTROY:
            m_clientHWnd = nullptr;// 窗口销毁时清空句柄
            return 0;

        default:
            // 修正：使用客户端窗口自身的句柄（m_clientHWnd）处理消息
            return DefWindowProcW(m_clientHWnd, msg, wParam, lParam);
    }
}

WindowsTrayPlugin::WindowsTrayPlugin(HINSTANCE hInstance,
                                     const wchar_t *className,
                                     const wchar_t *windowTitle,
                                     const wchar_t *tooltip)
    : m_hInstance(hInstance),
      m_className(className),
      m_windowTitle(windowTitle),
      m_tooltip(tooltip),
      m_clientClassName(L"AstraClientWindow"),// 初始化客户端窗口类名
      m_clientWindowTitle(L"Astra 客户端"),   // 初始化客户端窗口标题
      m_hWnd(nullptr),
      m_clientHWnd(nullptr) {
    ZeroMemory(&m_nid, sizeof(NOTIFYICONDATA));
}

WindowsTrayPlugin::~WindowsTrayPlugin() {
    DestroyTrayIcon();
    if (m_nid.hIcon) {
        DestroyIcon(m_nid.hIcon);// 释放自定义图标资源
    }
}


bool WindowsTrayPlugin::RegisterWindowClass() {
    WNDCLASSEXW wc = {0};// 使用WNDCLASSEXW结构体类型

    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = m_className.c_str();

    // 注册窗口类，使用宽字符版本
    return RegisterClassExW(&wc) != 0;
}

int WindowsTrayPlugin::Run() {
    if (!RegisterWindowClass()) {
        MessageBoxW(NULL, L"窗口注册失败！", L"错误", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // 创建窗口，使用宽字符版本
    m_hWnd = CreateWindowExW(
            0,
            m_className.c_str(),
            m_windowTitle.c_str(),
            0,// 隐藏窗口
            CW_USEDEFAULT, CW_USEDEFAULT, 240, 120,
            NULL, NULL, m_hInstance, this);

    if (m_hWnd == NULL) {
        MessageBoxW(NULL, L"窗口创建失败！", L"错误", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // 创建托盘图标
    CreateTrayIcon();

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int) msg.wParam;
}

void WindowsTrayPlugin::CreateTrayIcon() {
    // 初始化NOTIFYICONDATA结构
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;

    // 使用自定义绘制的立体图标，替代系统默认图标
    m_nid.hIcon = CreateCustomIcon();

    // 设置提示文本
    if (!m_tooltip.empty()) {
        wcsncpy_s(m_nid.szTip, m_tooltip.c_str(), _TRUNCATE);
        m_nid.szTip[sizeof(m_nid.szTip) / sizeof(wchar_t) - 1] = L'\0';
    }

    // 添加图标到系统托盘
    Shell_NotifyIconW(NIM_ADD, &m_nid);
}

void WindowsTrayPlugin::ShowTrayMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();

    // 添加菜单项（使用宽字符版本）
    AppendMenuW(hMenu, MF_STRING, ID_ABOUT, L"关于");
    AppendMenuW(hMenu, MF_STRING, ID_OPEN_CLIENT, L"打开客户端");// 新增
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_EXIT, L"退出");

    // 设置前景窗口
    SetForegroundWindow(m_hWnd);

    // 显示菜单
    TrackPopupMenuEx(
            hMenu,
            TPM_BOTTOMALIGN | TPM_LEFTBUTTON,
            pt.x, pt.y,
            m_hWnd,
            NULL);

    // 确保菜单正确关闭
    PostMessageW(m_hWnd, WM_NULL, 0, 0);

    // 销毁菜单
    DestroyMenu(hMenu);
}

void WindowsTrayPlugin::DestroyTrayIcon() {
    if (m_hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
    }
}

void WindowsTrayPlugin::SetIcon(HICON hIcon) {
    if (hIcon) {
        m_nid.hIcon = hIcon;
        m_nid.uFlags = NIF_ICON;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}

void WindowsTrayPlugin::SetTooltip(const wchar_t *tooltip) {
    if (tooltip) {
        m_tooltip = tooltip;
        // 使用正确的wcsncpy_s用法
        wcsncpy_s(m_nid.szTip, m_tooltip.c_str(), _TRUNCATE);
        m_nid.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, reinterpret_cast<PNOTIFYICONDATAW>(&m_nid));
    }
}

LRESULT CALLBACK WindowsTrayPlugin::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 存储和获取类实例指针
    if (msg == WM_NCCREATE) {
        LPCREATESTRUCTW pCreateStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    } else {
        WindowsTrayPlugin *pThis = reinterpret_cast<WindowsTrayPlugin *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (pThis) {
            return pThis->HandleMessage(msg, wParam, lParam);
        }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT WindowsTrayPlugin::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON: {
            // 处理托盘图标事件
            if (lParam == WM_LBUTTONDOWN) {
                MessageBoxW(m_hWnd, L"系统托盘示例", L"提示", MB_OK);
            } else if (lParam == WM_RBUTTONDOWN) {
                // 右键点击显示菜单
                POINT pt;
                GetCursorPos(&pt);
                ShowTrayMenu(pt);
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_ABOUT:
                    MessageBoxW(m_hWnd, L"关于 Astra 缓存服务", L"关于", MB_OK);
                    break;
                case ID_OPEN_CLIENT:
                    ShowClientWindow();// 调用新添加的显示客户端窗口方法
                    break;
                case ID_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(m_hWnd, msg, wParam, lParam);
    }
    return 0;
}

HICON WindowsTrayPlugin::CreateCustomIcon() {
    constexpr int ICON_SIZE = 32;
    HDC hdc = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hBmp = CreateCompatibleBitmap(hdc, ICON_SIZE, ICON_SIZE);
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    // 设置背景透明
    SetBkMode(memDC, TRANSPARENT);

    // 1. 绘制背景（透明）
    RECT rect = {0, 0, ICON_SIZE, ICON_SIZE};
    HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240));// 仅用于预览的背景色
    FillRect(memDC, &rect, bgBrush);
    DeleteObject(bgBrush);

    // 2. 绘制数据库圆柱体
    // 圆柱体颜色渐变（从顶部到底部）
    GRADIENT_RECT gRect = {0, 1};
    TRIVERTEX vert[2] = {
            {6, 4, 0x0000, 0xA000, 0xFF00, 0x0000},                       // 顶部更亮
            {ICON_SIZE - 6, ICON_SIZE - 3, 0x0000, 0x5000, 0xAA00, 0x0000}// 底部更深
    };
    GradientFill(memDC, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_H);

    // 3. 绘制圆柱体轮廓
    HPEN outlinePen = CreatePen(PS_SOLID, 2, RGB(0, 40, 100));// 更深的边框颜色
    SelectObject(memDC, outlinePen);

    // 顶部椭圆
    Ellipse(memDC, 5, 1, ICON_SIZE - 5, 15);// 更宽的顶部椭圆
    // 底部椭圆
    Ellipse(memDC, 3, ICON_SIZE - 15, ICON_SIZE - 3, ICON_SIZE - 1);// 更宽的底部椭圆
    // 两侧直线
    MoveToEx(memDC, 5, 5, NULL);
    LineTo(memDC, 5, ICON_SIZE - 5);
    MoveToEx(memDC, ICON_SIZE - 5, 5, NULL);
    LineTo(memDC, ICON_SIZE - 5, ICON_SIZE - 5);

    // 4. 添加堆叠效果（表示多个表）
    HPEN stackPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    SelectObject(memDC, stackPen);
    for (int i = 0; i < 3; i++) {
        int yPos = 18 + i * 6;
        MoveToEx(memDC, 12, yPos, NULL);
        LineTo(memDC, ICON_SIZE - 12, yPos);
    }

    // 5. 添加"DB"文字标识
    HFONT hFont = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_SWISS, L"Arial");
    HGDIOBJ oldFont = SelectObject(memDC, hFont);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT textRect = {0, 6, ICON_SIZE, 22};
    DrawTextW(memDC, L"DB", 2, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(memDC, oldFont);
    DeleteObject(hFont);

    // 6. 添加高光效果
    HPEN highlightPen = CreatePen(PS_SOLID, 1, RGB(200, 220, 255));
    SelectObject(memDC, highlightPen);
    Arc(memDC, 10, 5, ICON_SIZE - 10, 11, ICON_SIZE - 12, 6, 12, 6);

    // 7. 转换为图标
    ICONINFO iconInfo = {0};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = hBmp;
    iconInfo.hbmMask = CreateBitmap(ICON_SIZE, ICON_SIZE, 1, 1, NULL);
    HICON hIcon = CreateIconIndirect(&iconInfo);

    // 清理资源
    DeleteObject(outlinePen);
    DeleteObject(stackPen);
    DeleteObject(highlightPen);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdc);
    DeleteObject(hBmp);
    DeleteObject(iconInfo.hbmMask);

    return hIcon;
}