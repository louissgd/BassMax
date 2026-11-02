// bassmax_tray.cpp  —  托盘图标线程 + 设置窗口
// 交互: 左键单击 = 打开/收起设置窗口; 右键单击 = 直接切总开关 (开启 <-> 关闭)
// 图标: 开启用彩色 (IDI_APP), 关闭用灰色 (IDI_APP_OFF)
#include "bassmax_tray.h"
#include "bassmax_ui.h"
#include "resource.h"
#include <shellapi.h>

extern HINSTANCE g_hInst;

namespace {

const UINT      kTrayMsg  = WM_APP + 101;
const UINT      kSyncMsg  = WM_APP + 102;   // 设置窗口改了总开关 -> 刷新托盘图标
const UINT_PTR  kTimerId  = 1;              // 兜底: 其他窗口 (属性页) 改了设置也能跟上
const UINT      kTrayId   = 1;
const wchar_t*  kWndClass = L"BassMaxTrayWnd";

CRITICAL_SECTION g_cs;
bool             g_csInit = false;
int              g_ref    = 0;
HANDLE           g_thread = nullptr;
HWND             g_trayWnd = nullptr;   // 托盘线程里的隐藏窗口
HWND             g_dlg     = nullptr;   // 设置窗口 (在托盘线程创建)

// 以下只在托盘线程访问
NOTIFYICONDATAW  g_nid    = {};
HICON            g_icoOn  = nullptr;
HICON            g_icoOff = nullptr;
LONG             g_lastGen = -1;

void EnsureCs() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }

HICON LoadAppIcon(int id, int cx, int cy) {
    return (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(id), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

// 按当前 enabled 状态刷新托盘图标与提示文字 (托盘线程)
void ApplyTrayIcon() {
    if (!g_nid.hWnd) return;
    bool on = bassmax::GetSettings().enabled;

    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    HICON h = on ? g_icoOn : g_icoOff;
    if (h) g_nid.hIcon = h;
    wcscpy_s(g_nid.szTip, on
             ? L"BassMax · 已开启\n左键设置   右键关闭"
             : L"BassMax · 已关闭\n左键设置   右键开启");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// 右键: 直接翻转总开关, 参数全部保留
void ToggleEnabled() {
    bassmax::Settings s = bassmax::GetSettings();
    s.enabled = !s.enabled;
    g_lastGen = bassmax::SetSettings(s);      // 音频线程下一拍就生效
    ApplyTrayIcon();
    if (g_dlg) {                              // 设置窗口开着就同步勾选框
        bassmax::UiLoadFromSettings(g_dlg);
    }
}

INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM w, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        bassmax::UiLoadFromSettings(hwnd);
        HICON hIcon = LoadAppIcon(IDI_APP, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON));
        if (hIcon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        HICON hBig = LoadAppIcon(IDI_APP, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON));
        if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hBig);
        return TRUE;
    }
    case WM_HSCROLL:
        bassmax::UiUpdateLabels(hwnd);
        g_lastGen = bassmax::SetSettings(bassmax::UiReadSettings(hwnd));   // 实时生效并保存
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) { DestroyWindow(hwnd); return TRUE; }
        if (id == IDC_CHK_ON) {                       // 总开关: 立即生效
            g_lastGen = bassmax::SetSettings(bassmax::UiReadSettings(hwnd));
            bassmax::UiUpdateLabels(hwnd);
            if (g_trayWnd) PostMessageW(g_trayWnd, kSyncMsg, 0, 0);   // 换彩色/灰色图标
            return TRUE;
        }
        if (bassmax::UiPresetClicked(hwnd, id) >= 0) {
            g_lastGen = bassmax::SetSettings(bassmax::UiReadSettings(hwnd));   // 换风格立即生效
            bassmax::UiUpdateLabels(hwnd);
        }
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return TRUE;
    case WM_DESTROY:
        g_dlg = nullptr;
        return TRUE;
    }
    return FALSE;
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case kTrayMsg:
        if (LOWORD(l) == WM_LBUTTONUP) {          // 左键: 设置窗口
            bassmax::ShowTraySettings();
        } else if (LOWORD(l) == WM_RBUTTONUP) {   // 右键: 直接开关
            ToggleEnabled();
        }
        return 0;
    case kSyncMsg:
        ApplyTrayIcon();
        return 0;
    case WM_TIMER:
        if (w == kTimerId) {
            LONG gen = bassmax::SettingsGen();
            if (gen != g_lastGen) {               // 属性页等外部改动
                g_lastGen = gen;
                ApplyTrayIcon();
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

DWORD WINAPI TrayThread(LPVOID) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = TrayWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kWndClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, g_hInst, nullptr);
    if (!hwnd) return 0;

    EnterCriticalSection(&g_cs);
    g_trayWnd = hwnd;
    LeaveCriticalSection(&g_cs);

    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    g_icoOn  = LoadAppIcon(IDI_APP, cx, cy);
    g_icoOff = LoadAppIcon(IDI_APP_OFF, cx, cy);
    if (!g_icoOff) g_icoOff = g_icoOn;

    bool on = bassmax::GetSettings().enabled;
    g_lastGen = bassmax::SettingsGen();

    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = kTrayId;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon = on ? g_icoOn : g_icoOff;
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));   // 兜底
    wcscpy_s(g_nid.szTip, on
             ? L"BassMax · 已开启\n左键设置   右键关闭"
             : L"BassMax · 已关闭\n左键设置   右键开启");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    SetTimer(hwnd, kTimerId, 400, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    KillTimer(hwnd, kTimerId);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);

    HICON a = g_icoOn, b = g_icoOff;
    g_icoOn = g_icoOff = nullptr;
    if (a) DestroyIcon(a);
    if (b && b != a) DestroyIcon(b);
    g_nid = {};
    return 0;
}

} // namespace

namespace bassmax {

void StartTray() {
    EnsureCs();
    EnterCriticalSection(&g_cs);
    if (g_ref++ == 0) {
        DWORD tid = 0;
        g_thread = CreateThread(nullptr, 0, TrayThread, nullptr, 0, &tid);
    }
    LeaveCriticalSection(&g_cs);
}

void StopTray() {
    EnsureCs();
    EnterCriticalSection(&g_cs);
    bool last = (g_ref > 0) && (--g_ref == 0);
    HWND w = g_trayWnd;
    HANDLE th = g_thread;
    if (last) { g_trayWnd = nullptr; g_thread = nullptr; }
    LeaveCriticalSection(&g_cs);

    if (last) {
        if (w) PostMessageW(w, WM_CLOSE, 0, 0);
        if (th) { WaitForSingleObject(th, 2000); CloseHandle(th); }
    }
}

namespace {
// 把窗口摆到当前显示器工作区正中 (避开任务栏)
void CenterOnScreen(HWND h) {
    RECT rc; GetWindowRect(h, &rc);
    int w = rc.right - rc.left, ht = rc.bottom - rc.top;
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT wa;
    if (GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - w) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - ht) / 2;
    SetWindowPos(h, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}
}

void ShowTraySettings() {
    if (g_dlg) {
        // 已经开着: 再点一下就收起来
        if (IsWindowVisible(g_dlg)) { ShowWindow(g_dlg, SW_HIDE); return; }
        ShowWindow(g_dlg, SW_SHOW);
        SetForegroundWindow(g_dlg);
        return;
    }
    g_dlg = CreateDialogW(g_hInst, MAKEINTRESOURCEW(IDD_BASSMAX_POPUP), nullptr, SettingsDlgProc);
    if (g_dlg) {
        CenterOnScreen(g_dlg);
        ShowWindow(g_dlg, SW_SHOW);
        SetForegroundWindow(g_dlg);
    }
}

} // namespace bassmax
