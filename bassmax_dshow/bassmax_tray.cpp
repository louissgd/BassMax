// bassmax_tray.cpp  —  托盘图标线程 + 设置窗口 + 独立设置入口
// 交互: 左键单击 = 打开/收起设置窗口; 右键单击 = 直接切总开关 (开启 <-> 关闭)
// 图标: 开启用彩色 (IDI_APP), 关闭用灰色 (IDI_APP_OFF)
//
// 线程除了显示图标, 还按 400ms 轮询注册表:
//   - TrayIcon 改成 0/1 -> 立刻隐藏/显示图标 (不用重开播放器)
//   - Bass/Treble/Drive/Preset/Enabled -> 立刻套用 (手改注册表或独立设置程序都算)
#include "bassmax_tray.h"
#include "bassmax_ui.h"
#include "resource.h"
#include <shellapi.h>

extern HINSTANCE g_hInst;

namespace {

const UINT      kTrayMsg  = WM_APP + 101;
const UINT      kSyncMsg  = WM_APP + 102;   // 设置窗口改了总开关 -> 刷新托盘图标
const UINT_PTR  kTimerId  = 1;              // 轮询注册表 (图标显隐 + 参数热加载)
const UINT      kTrayId   = 1;
const wchar_t*  kWndClass = L"BassMaxTrayWnd";

CRITICAL_SECTION g_cs;
bool             g_csInit = false;
int              g_ref    = 0;
HANDLE           g_thread = nullptr;
HWND             g_trayWnd = nullptr;   // 托盘线程里的隐藏窗口
HWND             g_dlg     = nullptr;   // 设置窗口 (在托盘线程创建)
bool             g_solo    = false;     // 独立设置模式: 窗口关闭即退出消息循环

// 以下只在托盘线程访问
NOTIFYICONDATAW  g_nid       = {};
HICON            g_icoOn     = nullptr;
HICON            g_icoOff    = nullptr;
bool             g_iconShown = false;
UINT             g_taskbarCreated = 0;
LONG             g_lastGen   = -1;

void EnsureCs() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }

HICON LoadAppIcon(int id, int cx, int cy) {
    return (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(id), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

void SetTip(bool on) {
    wcscpy_s(g_nid.szTip, on
             ? L"BassMax · 已开启\n左键设置   右键关闭"
             : L"BassMax · 已关闭\n左键设置   右键开启");
}

// 真正把图标放进任务栏 (托盘线程)
void ShowIcon() {
    if (!g_nid.hWnd || g_iconShown) return;
    bool on = bassmax::GetSettings().enabled;

    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.hIcon  = on ? g_icoOn : g_icoOff;
    if (!g_nid.hIcon) g_nid.hIcon = g_icoOn;                       // 灰版缺失时退回彩版
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    SetTip(on);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_iconShown = true;
}

void HideIcon() {
    if (!g_nid.hWnd || !g_iconShown) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_iconShown = false;
}

// 按当前 enabled 状态刷新图标与提示文字 (托盘线程)
void ApplyTrayIcon() {
    if (!g_nid.hWnd || !g_iconShown) return;      // 未显示就不动 (ShowIcon 会处理)
    bool on = bassmax::GetSettings().enabled;

    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    HICON h = on ? g_icoOn : g_icoOff;
    if (h) g_nid.hIcon = h;
    SetTip(on);
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
        if (g_solo) PostQuitMessage(0);               // 独立设置模式: 关窗即退出
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
            // 注册表轮询: 手改 Bass/Treble/Drive/Preset/Enabled 或独立设置程序写入
            // 都会在这里被发现 -> 变更代号 +1 -> 音频线程重建系数。
            LONG gen = bassmax::ReloadFromRegistry();
            if (gen != g_lastGen) {
                g_lastGen = gen;
                ApplyTrayIcon();
                if (g_dlg) bassmax::UiLoadFromSettings(g_dlg);
            }
            // TrayIcon 0/1 实时显隐
            if (bassmax::TrayEnabled()) ShowIcon(); else HideIcon();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    // 资源管理器重启后任务栏图标会消失, 收到广播就重新挂上
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        g_iconShown = false;
        if (bassmax::TrayEnabled()) ShowIcon();
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

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_lastGen = bassmax::SettingsGen();

    g_nid = {};
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = kTrayId;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon            = g_icoOn;

    // 图标是否出现由注册表 TrayIcon 决定; 线程本身常驻, 这样改成 1 能立刻长出来
    if (bassmax::TrayEnabled()) ShowIcon();
    SetTimer(hwnd, kTimerId, 400, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    KillTimer(hwnd, kTimerId);
    HideIcon();

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

// 独立设置窗口: 不依赖播放器, 建好窗口后自己跑消息循环, 关闭时返回。
int RunStandaloneSettings() {
    InitSettings();
    if (g_dlg) {                                   // 理论上不会, 防御一下
        ShowWindow(g_dlg, SW_SHOW);
        SetForegroundWindow(g_dlg);
        return 0;
    }

    g_solo = true;
    HWND h = CreateDialogW(g_hInst, MAKEINTRESOURCEW(IDD_BASSMAX_POPUP), nullptr, SettingsDlgProc);
    if (!h) { g_solo = false; return 1; }

    g_dlg = h;
    CenterOnScreen(h);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsWindow(h) && IsDialogMessageW(h, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_solo = false;
    return 0;
}

} // namespace bassmax

// ---- 独立设置入口 ----------------------------------------------------------
// 播放器不启动也能调出设置, 两种用法:
//   1) BassMaxConfig.exe                (随安装包一起放的启动器, 双击即可)
//   2) rundll32.exe bassmax_dshow.ax,BassMaxConfig
// 用的是同一个 IDD_BASSMAX_POPUP 对话框, 改完立即写注册表;
// 正在播放的播放器会在 400ms 内从注册表热加载并生效。
extern "C" __declspec(dllexport) void CALLBACK BassMaxConfig(HWND, HINSTANCE, LPWSTR, int) {
    bassmax::RunStandaloneSettings();
}
