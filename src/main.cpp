#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <cwchar>
#include <initializer_list>
#include "tab_filter.h"

namespace {
constexpr wchar_t kClass[] = L"RhythmGuard.AltTab.Window.v1";
constexpr wchar_t kTitle[] = L"Rhythm Guard | 알탭 차단기";
constexpr UINT kEngineState = WM_APP + 1;
constexpr UINT kToggle = WM_APP + 2;
constexpr UINT kDisable = WM_APP + 3;
constexpr UINT kTray = WM_APP + 4;
constexpr int kToggleButton = 101, kHideButton = 102, kExitButton = 103;
constexpr int kOpenMenu = 104, kDisableMenu = 105;
constexpr COLORREF kBackground = RGB(16, 21, 31);
constexpr COLORREF kCard = RGB(26, 34, 48);
constexpr COLORREF kText = RGB(239, 244, 252);
constexpr COLORREF kMuted = RGB(158, 174, 195);
constexpr COLORREF kGreen = RGB(99, 226, 177);
constexpr COLORREF kRed = RGB(255, 153, 153);

enum class State : WPARAM { Starting, Off, On, HookError, ThreadError };

struct Engine {
    HWND owner = nullptr;
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    HHOOK hook = nullptr;
    bool enabled = false;
    bool leftAlt = false, rightAlt = false;
    TabFilter filter;

    void Notify(State state, DWORD error = 0) const {
        PostMessageW(owner, kEngineState, static_cast<WPARAM>(state), error);
    }

    void SetEnabled(bool value);
    static DWORD WINAPI Run(void* context);
    static LRESULT CALLBACK Keyboard(int code, WPARAM message, LPARAM data);
} gEngine;

struct Ui {
    HWND window = nullptr, toggle = nullptr, hide = nullptr, exit = nullptr;
    State state = State::Starting;
    DWORD error = 0;
    bool trayAdded = false;
    UINT taskbarCreated = 0;
    UINT dpi = 96;
    HFONT heading = nullptr, body = nullptr, small = nullptr, status = nullptr;
    HICON iconOn = nullptr, iconOff = nullptr;
} gUi;

LRESULT CALLBACK Engine::Keyboard(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && gEngine.enabled) {
        const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
        if ((down || up) && (key.vkCode == VK_LMENU || key.vkCode == VK_RMENU || key.vkCode == VK_MENU)) {
            bool& alt = key.vkCode == VK_RMENU ||
                        (key.vkCode == VK_MENU && (key.flags & LLKHF_EXTENDED))
                            ? gEngine.rightAlt : gEngine.leftAlt;
            alt = down;
        }
        if (key.vkCode == VK_TAB) {
            // Ctrl+Alt+Tab may lack the Alt context flag. Track both Alt keys
            // from their own events so that variant cannot open the switcher.
            const bool altDown = (key.flags & LLKHF_ALTDOWN) != 0 ||
                                 gEngine.leftAlt || gEngine.rightAlt;
            if ((down || up) && gEngine.filter.Suppress(down, altDown)) {
                return 1;
            }
        }
    }
    // No UI, logging, file access, or waiting in the timing-sensitive callback.
    return CallNextHookEx(nullptr, code, message, data);
}

void Engine::SetEnabled(bool value) {
    if (value == enabled) {
        Notify(enabled ? State::On : State::Off);
        return;
    }
    if (value) {
        // This snapshot is intentionally outside the hook callback. A key-up
        // for a Tab held before enabling must still reach its original target.
        filter.Reset((GetAsyncKeyState(VK_TAB) & 0x8000) != 0);
        leftAlt = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
        rightAlt = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
        hook = SetWindowsHookExW(WH_KEYBOARD_LL, Keyboard, GetModuleHandleW(nullptr), 0);
        if (!hook) {
            Notify(State::HookError, GetLastError());
            return;
        }
        enabled = true;
    } else {
        // Even if Windows has already removed a timed-out hook, the callback
        // must pass everything once the user requests release.
        enabled = false;
        if (hook) UnhookWindowsHookEx(hook);
        hook = nullptr;
        filter.Reset();
        leftAlt = rightAlt = false;
    }
    Notify(enabled ? State::On : State::Off);
}

DWORD WINAPI Engine::Run(void* context) {
    auto& engine = *static_cast<Engine*>(context);
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    // Opening the app is the user's explicit opt-in; no global hotkeys.
    engine.SetEnabled(true);

    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == kToggle) {
            engine.SetEnabled(!engine.enabled);
        } else if (message.message == kDisable) {
            engine.SetEnabled(false);
        }
    }
    engine.SetEnabled(false);
    if (result < 0) engine.Notify(State::ThreadError, GetLastError());
    return 0;
}

int Px(int value) { return MulDiv(value, static_cast<int>(gUi.dpi), 96); }
bool IsOn() { return gUi.state == State::On; }
bool IsReady() {
    return gUi.state == State::Off || gUi.state == State::On || gUi.state == State::HookError;
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Rounded(HDC dc, RECT rect, COLORREF color, int radius = 14) {
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, Px(radius), Px(radius));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

void Text(HDC dc, const wchar_t* text, int x, int y, int width, int height,
          HFONT font, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    RECT rect{Px(x), Px(y), Px(x + width), Px(y + height)};
    HGDIOBJ previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

void RebuildFonts() {
    for (HFONT font : {gUi.heading, gUi.body, gUi.small, gUi.status}) {
        if (font) DeleteObject(font);
    }
    auto make = [](int size, int weight) {
        return CreateFontW(-Px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
    };
    gUi.heading = make(27, FW_BOLD);
    gUi.body = make(15, FW_NORMAL);
    gUi.small = make(13, FW_NORMAL);
    gUi.status = make(22, FW_BOLD);
    for (HWND button : {gUi.toggle, gUi.hide, gUi.exit}) {
        if (button) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(gUi.body), TRUE);
    }
}

void Layout() {
    MoveWindow(gUi.toggle, Px(28), Px(224), Px(464), Px(54), TRUE);
    MoveWindow(gUi.hide, Px(28), Px(369), Px(322), Px(40), TRUE);
    MoveWindow(gUi.exit, Px(362), Px(369), Px(130), Px(40), TRUE);
}

void ShowWindowAgain() {
    ShowWindow(gUi.window, SW_RESTORE);
    SetForegroundWindow(gUi.window);
}

NOTIFYICONDATAW TrayData() {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = gUi.window;
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    icon.uCallbackMessage = kTray;
    icon.hIcon = IsOn() ? gUi.iconOn : gUi.iconOff;
    wcscpy_s(icon.szTip, IsOn() ? L"Rhythm Guard | Alt+Tab 차단 중" : L"Rhythm Guard | 차단 꺼짐");
    return icon;
}

void UpdateTray() {
    auto icon = TrayData();
    if (gUi.trayAdded && Shell_NotifyIconW(NIM_MODIFY, &icon)) return;
    gUi.trayAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    if (gUi.trayAdded) {
        icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon);
    }
}

void HideToTray() {
    UpdateTray();
    if (gUi.trayAdded) ShowWindow(gUi.window, SW_HIDE);
    else MessageBoxW(gUi.window, L"트레이 아이콘을 만들지 못했습니다. 창을 켜둔 채 사용해주세요.",
                     kTitle, MB_OK | MB_ICONINFORMATION);
}

void UpdateState(State state, DWORD error) {
    gUi.state = state;
    gUi.error = error;
    const bool failed = state == State::HookError || state == State::ThreadError;
    wchar_t errorTitle[120]{};
    if (failed) swprintf_s(errorTitle, L"Rhythm Guard | 차단 꺼짐 | Windows 오류 %lu",
                          static_cast<unsigned long>(error));
    SetWindowTextW(gUi.window, failed ? errorTitle : (IsOn() ? L"Rhythm Guard | Alt+Tab 차단 중" : kTitle));
    SetWindowTextW(gUi.toggle, IsOn() ? L"차단 끄기" : L"차단 켜기");
    EnableWindow(gUi.toggle, IsReady());
    SendMessageW(gUi.window, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(IsOn() ? gUi.iconOn : gUi.iconOff));
    SendMessageW(gUi.window, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(IsOn() ? gUi.iconOn : gUi.iconOff));
    UpdateTray();
    InvalidateRect(gUi.window, nullptr, FALSE);
    if (!IsReady() && state != State::Starting) ShowWindowAgain();
}

void Paint(HDC dc) {
    RECT client{};
    GetClientRect(gUi.window, &client);
    Fill(dc, client, kBackground);
    Text(dc, L"RHYTHM GUARD", 28, 23, 464, 22, gUi.small, kGreen);
    Text(dc, L"플레이에만 집중해.", 28, 52, 464, 43, gUi.heading, kText);
    Text(dc, L"리듬게임을 위한 Alt + Tab 차단기", 28, 98, 464, 25, gUi.body, kMuted);

    Rounded(dc, {Px(28), Px(140), Px(492), Px(208)}, kCard);
    const bool failed = gUi.state == State::HookError || gUi.state == State::ThreadError;
    const COLORREF accent = failed ? kRed : (IsOn() ? kGreen : kMuted);
    Rounded(dc, {Px(46), Px(160), Px(54), Px(168)}, accent, 8);
    const wchar_t* title = L"차단 꺼짐";
    const wchar_t* detail = L"잠시 해제 중입니다. 버튼으로 다시 켤 수 있어요.";
    if (IsOn()) {
        title = L"Alt + Tab 차단 중";
        detail = L"켜져 있는 동안 모든 프로그램에 적용됩니다.";
    } else if (gUi.state == State::Starting) {
        title = L"준비 중";
        detail = L"Alt + Tab 차단을 준비하고 있어요.";
    } else if (failed) {
        title = L"차단을 시작할 수 없습니다";
        detail = L"현재 차단은 꺼져 있습니다. 다시 실행해주세요.";
    }
    Text(dc, title, 66, 146, 410, 31, gUi.status, accent);
    Text(dc, detail, 46, 177, 430, 24, gUi.small, kMuted);

    Text(dc, L"Alt + Shift + Tab / Ctrl + Alt + Tab도 차단합니다.",
         30, 297, 462, 24, gUi.small, kMuted);
    Text(dc, L"Tab 단독 입력과 다른 키는 그대로 사용할 수 있어요.",
         30, 325, 462, 24, gUi.small, kMuted);
    if (failed) {
        wchar_t message[80]{};
        swprintf_s(message, L"Windows 오류: %lu", static_cast<unsigned long>(gUi.error));
        Text(dc, message, 30, 423, 462, 18, gUi.small, kRed);
    } else {
        Text(dc, L"전역 단축키 없음 · 창을 닫으면 차단도 해제됩니다.",
             30, 423, 462, 18, gUi.small, kMuted);
    }
}

void PaintButton(const DRAWITEMSTRUCT& item) {
    const bool main = item.CtlID == kToggleButton;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    COLORREF fill = main ? (IsOn() ? RGB(67, 87, 119) : kGreen) : kCard;
    if (disabled) fill = RGB(42, 50, 64);
    if ((item.itemState & ODS_SELECTED) != 0) fill = RGB(73, 132, 125);
    Fill(item.hDC, item.rcItem, kBackground);
    Rounded(item.hDC, item.rcItem, fill, 12);
    wchar_t text[80]{};
    GetWindowTextW(item.hwndItem, text, 80);
    HGDIOBJ old = SelectObject(item.hDC, gUi.body);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? kMuted : (main && !IsOn() ? kBackground : kText));
    RECT rect = item.rcItem;
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(item.hDC, old);
    if ((item.itemState & ODS_FOCUS) != 0) {
        InflateRect(&rect, -Px(5), -Px(5));
        DrawFocusRect(item.hDC, &rect);
    }
}

void TrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kOpenMenu, L"창 열기");
    AppendMenuW(menu, MF_STRING | (IsReady() ? 0 : MF_GRAYED), kToggleButton,
                IsOn() ? L"차단 끄기" : L"차단 켜기");
    AppendMenuW(menu, MF_STRING, kDisableMenu, L"즉시 해제");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitButton, L"종료");
    POINT position{};
    GetCursorPos(&position);
    SetForegroundWindow(gUi.window);
    const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         position.x, position.y, 0, gUi.window, nullptr);
    DestroyMenu(menu);
    if (selected) PostMessageW(gUi.window, WM_COMMAND, selected, 0);
    PostMessageW(gUi.window, WM_NULL, 0, 0);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (gUi.taskbarCreated && message == gUi.taskbarCreated) {
        gUi.trayAdded = false;
        UpdateTray();
        // If Explorer cannot restore the icon, never leave a hidden controller.
        if (!gUi.trayAdded) ShowWindowAgain();
        return 0;
    }
    switch (message) {
    case WM_CREATE: {
        gUi.window = window;
        gUi.dpi = GetDpiForWindow(window);
        auto button = [&](int id, const wchar_t* label) {
            return CreateWindowExW(0, L"BUTTON", label,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
        };
        gUi.toggle = button(kToggleButton, L"차단 켜기");
        gUi.hide = button(kHideButton, L"트레이로 숨기기");
        gUi.exit = button(kExitButton, L"종료");
        EnableWindow(gUi.toggle, FALSE);
        RebuildFonts();
        Layout();
        gUi.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        UpdateTray();
        gEngine.owner = window;
        gEngine.thread = CreateThread(nullptr, 0, Engine::Run, &gEngine, 0, &gEngine.threadId);
        if (!gEngine.thread) UpdateState(State::ThreadError, GetLastError());
        return 0;
    }
    case kEngineState:
        UpdateState(static_cast<State>(wParam), static_cast<DWORD>(lParam));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kToggleButton:
            if (IsReady()) PostThreadMessageW(gEngine.threadId, kToggle, 0, 0);
            break;
        case kDisableMenu:
            if (IsReady()) PostThreadMessageW(gEngine.threadId, kDisable, 0, 0);
            break;
        case kHideButton: HideToTray(); break;
        case kOpenMenu: ShowWindowAgain(); break;
        case kExitButton: SendMessageW(window, WM_CLOSE, 0, 0); break;
        }
        return 0;
    case kTray:
        if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT ||
            LOWORD(lParam) == WM_LBUTTONDBLCLK) ShowWindowAgain();
        else if (LOWORD(lParam) == WM_CONTEXTMENU) TrayMenu();
        return 0;
    case WM_SIZE:
        // Minimize keeps a taskbar button; only the explicit hide button hides.
        return 0;
    case WM_DPICHANGED: {
        gUi.dpi = HIWORD(wParam);
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RebuildFonts();
        Layout();
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
        HGDIOBJ previous = SelectObject(buffer, bitmap);
        Paint(buffer);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, previous);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DRAWITEM:
        PaintButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_CLOSE:
        if (gEngine.thread) {
            PostThreadMessageW(gEngine.threadId, WM_QUIT, 0, 0);
            // Ending the process also releases hooks if a damaged input thread
            // cannot complete shutdown. Never terminate a thread in-place.
            WaitForSingleObject(gEngine.thread, 2000);
            CloseHandle(gEngine.thread);
            gEngine.thread = nullptr;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY: {
        if (gUi.trayAdded) {
            auto icon = TrayData();
            Shell_NotifyIconW(NIM_DELETE, &icon);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\RhythmGuard.AltTab.Singleton.v1");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClass, nullptr)) {
            ShowWindowAsync(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    gUi.iconOff = LoadIconW(instance, MAKEINTRESOURCEW(201));
    gUi.iconOn = LoadIconW(instance, MAKEINTRESOURCEW(202));
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = gUi.iconOff;
    windowClass.hIconSm = gUi.iconOff;
    windowClass.lpszClassName = kClass;
    if (!RegisterClassExW(&windowClass)) {
        CloseHandle(mutex);
        return 1;
    }
    const UINT dpi = GetDpiForSystem();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect{0, 0, MulDiv(520, dpi, 96), MulDiv(452, dpi, 96)};
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    HWND window = CreateWindowExW(0, kClass, kTitle, style, x, y, width, height,
                                   nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"프로그램 창을 만들지 못했습니다.", kTitle, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (result < 0 && IsWindow(window)) SendMessageW(window, WM_CLOSE, 0, 0);
    for (HFONT font : {gUi.heading, gUi.body, gUi.small, gUi.status}) {
        if (font) DeleteObject(font);
    }
    CloseHandle(mutex);
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
