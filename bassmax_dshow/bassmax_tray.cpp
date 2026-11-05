// bassmax_tray.cpp  —  托盘图标线程 + 设置窗口 + 独立设置入口
// 交互: 左键单击 = 打开/收起设置窗口; 右键单击 = 直接切总开关 (开启 <-> 关闭)
// 图标: 开启用彩色 (IDI_APP), 关闭用灰色 (IDI_APP_OFF)
//
// 线程除了显示图标, 还按 400ms 轮询注册表:
//   - TrayIcon 改成 0/1 -> 立刻隐藏/显示图标 (不用重开播放器)
//   - Bass/Treble/Drive/Preset/Enabled -> 立刻套用 (手改注册表或独立设置程序都算)
//
// ---- 线程生命周期 (1.0.4 重做) ----------------------------------------------
// 旧实现: 每来一个滤镜实例就 CreateThread, 最后一个实例析构时 PostMessage(WM_CLOSE)
// 然后再 WaitForSingleObject(2000)。两个致命问题:
//   1) 析构发生在播放器拆图的线程上 (切音轨 = 拆音频支路), 等 2 秒就是画面停 2 秒;
//      托盘线程若正好卡在 Shell_NotifyIcon 的跨进程调用里, 就是永久卡死。
//   2) 上一个线程还没退干净, 新实例又开一个 -> 两个线程抢同一套 g_nid / HICON,
//      互相 NIM_DELETE、DestroyIcon, 图标乱跳甚至崩。
// 现在: 线程每进程只开一个, 活得和进程一样久 (DllCanUnloadNow 恒为 S_FALSE),
// 只靠 g_ref 决定"图标该不该显示"。析构只发一条异步消息, 立即返回, 永不阻塞。
#include "bassmax_tray.h"
#include "bassmax_ui.h"
#include "resource.h"
#include "version.h"
#include <shellapi.h>

extern HINSTANCE g_hInst;

namespace {

const UINT      kTrayMsg  = WM_APP + 101;
const UINT      kSyncMsg  = WM_APP + 102;   // 设置窗口改了总开关 -> 刷新托盘图标
const UINT      kWakeMsg  = WM_APP + 103;   // 有实例加入/离开 -> 立刻重算图标显隐
const UINT_PTR  kTimerId  = 1;              // 轮询注册表 (图标显隐 + 参数热加载)
const UINT      kTrayId   = 1;
const wchar_t*  kWndClass = L"BassMaxTrayWnd";

CRITICAL_SECTION g_cs;
bool             g_csInit = false;
LONG             g_ref    = 0;          // 活着的滤镜实例数 (g_cs 保护)
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

// 当前活着的滤镜实例数 (任何线程可调)
LONG TrayRef() {
    EnsureCs();
    EnterCriticalSection(&g_cs);
    LONG r = g_ref;
    LeaveCriticalSection(&g_cs);
    return r;
}

HICON LoadAppIcon(int id, int cx, int cy) {
    return (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(id), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

void SetTip(bool on) {
    wcscpy_s(g_nid.szTip, on
             ? L"BassMax " BASSMAX_VER_STR_W L" · 已开启\n左键设置   右键关闭"
             : L"BassMax " BASSMAX_VER_STR_W L" · 已关闭\n左键设置   右键开启");
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

// 图标该不该在: 有活实例 + 用户没关 TrayIcon
void UpdateVisibility() {
    if (!g_nid.hWnd) return;
    if (TrayRef() > 0 && bassmax::TrayEnabled()) {
        if (!g_iconShown) ShowIcon();
        else              ApplyTrayIcon();   // 已经在了: 只刷开关状态/提示文字
    } else {
        HideIcon();
    }
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
    case kWakeMsg:
        UpdateVisibility();
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
            // 有实例 + TrayIcon 0/1 -> 实时显隐
            UpdateVisibility();
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

    // 图标是否出现由 实例数 + 注册表 TrayIcon 决定; 线程本身常驻进程生命周期
    UpdateVisibility();
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

    // 线程真退出了才清全局, 且只清自己的 —— 否则新实例会 PostMessage 到死窗口
    EnsureCs();
    EnterCriticalSection(&g_cs);
    if (g_trayWnd == hwnd) g_trayWnd = nullptr;
    LeaveCriticalSection(&g_cs);
    return 0;
}

} // namespace

namespace bassmax {

void StartTray() {
    EnsureCs();
    HANDLE need = nullptr;
    HWND   wake = nullptr;
    EnterCriticalSection(&g_cs);
    ++g_ref;
    if (!g_thread) {              // 每进程只开一个线程, 之后永久复用
        DWORD tid = 0;
        need = CreateThread(nullptr, 0, TrayThread, nullptr, 0, &tid);
        if (need) g_thread = need;
    } else {
        wake = g_trayWnd;         // 线程已在跑: 叫它重算图标显隐
    }
    LeaveCriticalSection(&g_cs);

    if (!need && wake) PostMessageW(wake, kWakeMsg, 0, 0);
    // 注意: 新建线程那条路径不需要 post, 线程起来自己会 UpdateVisibility()
}

void StopTray() {
    EnsureCs();
    bool idle = false;
    HWND wake = nullptr;
    EnterCriticalSection(&g_cs);
    if (g_ref > 0 && --g_ref == 0) idle = true;
    wake = g_trayWnd;
    LeaveCriticalSection(&g_cs);

    // 只发消息, 不等待、不 CloseHandle、不销毁线程。
    // 析构可能跑在播放器拆图/切音轨的线程上, 任何阻塞都会直接让画面停住。
    if (idle && wake) PostMessageW(wake, kWakeMsg, 0, 0);
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

// ---- 找本进程里已经开的设置窗口 (相同标题) ----
namespace {
HWND          g_find       = nullptr;
const wchar_t* g_findTitle = nullptr;

BOOL CALLBACK FindCfgWnd(HWND h, LPARAM) {
    wchar_t t[128] = {};
    GetWindowTextW(h, t, 128);
    if (g_findTitle && t[0] && wcscmp(t, g_findTitle) == 0) { g_find = h; return FALSE; }
    return TRUE;
}

// ---- 没有清单的宿主进程里临时把视觉样式和 DPI 感知补上 ----------------------
// 播放器自己带 comctl32 v6 清单, 属性页/托盘窗口天生就是主题化的;
// 而 rundll32.exe 没有清单, 直接 CreateDialog 会退回 Win95 经典外观, 且整个进程
// 处于 DPI 虚拟化状态 -> 文字被位图拉伸发虚。下面三步在独立模式里补齐:
//   1) SetProcessDPIAware  让 GDI 按真实 DPI 画字 (进程设过就失败, 无害)
//   2) 激活本模块内嵌的 v6 清单, 再加载 comctl32 -> 解析到 WinSxS 里的 v6
//   3) SetThemeAppProperties 打开控件与非客户区主题
class ScopedActCtx {
public:
    ~ScopedActCtx() {
        if (m_active) DeactivateActCtx(0, m_cookie);
        if (m_h != INVALID_HANDLE_VALUE) ReleaseActCtx(m_h);
    }

    void Begin() {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(g_hInst, path, MAX_PATH);

        ACTCTXW ac = {};
        ac.cbSize         = sizeof(ac);
        ac.dwFlags        = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
        ac.lpSource       = path;
        ac.hModule        = (HMODULE)g_hInst;
        ac.lpResourceName = MAKEINTRESOURCEW(1);      // 1 24 "bassmax.manifest"
        m_h = CreateActCtxW(&ac);
        if (m_h == INVALID_HANDLE_VALUE) return;
        m_active = ActivateActCtx(m_h, &m_cookie) != FALSE;
    }

private:
    HANDLE    m_h      = INVALID_HANDLE_VALUE;
    ULONG_PTR m_cookie = 0;
    bool      m_active = false;
};

void EnableVisualStylesForHost() {
    // 1) DPI: 只在进程还没决定时生效 (失败的返回码无需处理)
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        typedef BOOL (WINAPI *PFN_DPIAWARE)(void);
        PFN_DPIAWARE f = (PFN_DPIAWARE)GetProcAddress(u, "SetProcessDPIAware");
        if (f) f();
    }

    // 2) comctl32 若已被宿主按 v5 加载, 再怎么激活清单也换不掉了 —— 直接用现有的
    if (!GetModuleHandleW(L"comctl32.dll")) {
        HMODULE c = LoadLibraryW(L"comctl32.dll");        // 在激活的清单下 -> v6
        if (c) {
            struct ICCEX { DWORD cbSize; DWORD dwICC; };
            typedef BOOL (WINAPI *PFN_ICC)(const ICCEX*);
            PFN_ICC f = (PFN_ICC)GetProcAddress(c, "InitCommonControlsEx");
            if (f) {
                ICCEX icc = { sizeof(icc), 0x000000FFu | 0x00004000u | 0x00000004u };
                f(&icc);                                  // WIN95_CLASSES|STANDARD|BAR
            }
        }
    }

    // 3) 允许控件使用主题 (uxtheme 动态取, 不给 .ax 添静态依赖)
    HMODULE t = LoadLibraryW(L"uxtheme.dll");
    if (t) {
        typedef void (WINAPI *PFN_STAP)(DWORD);
        PFN_STAP f = (PFN_STAP)GetProcAddress(t, "SetThemeAppProperties");
        if (f) f(0x7);                                    // NC|CONTROLS|WEBCONTENT
    }
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

    // 已经开始过一个 (比如连点两次快捷方式)? 拉到前面就行, 别开第二个窗口
    wchar_t title[128] = {};
    LoadStringW(g_hInst, IDS_TITLE, title, _countof(title));
    if (title[0]) {
        g_findTitle = title;
        g_find = nullptr;
        EnumWindows(FindCfgWnd, 0);
        g_findTitle = nullptr;
        if (g_find) {
            if (IsIconic(g_find)) ShowWindow(g_find, SW_RESTORE);
            ShowWindow(g_find, SW_SHOW);
            SetForegroundWindow(g_find);
            FlashWindow(g_find, TRUE);
            return 0;
        }
    }

    // 先补样式再建窗口: 清单必须赶在 comctl32 首次加载之前激活, 否则无效
    ScopedActCtx act;
    act.Begin();
    EnableVisualStylesForHost();

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

// ---- 独立设置入口 (rundll32) -----------------------------------------------
// 播放器没启动时打开设置窗口, 一律走系统自带的 rundll32, 不再附带单独的 EXE:
//     rundll32.exe "C:\...\bassmax_dshow.ax",BassMaxConfig
// 用的是同一个 IDD_BASSMAX_POPUP 对话框, 改完立即写注册表;
// 正在播放的播放器会在 400ms 内从注册表热加载并生效。
//
// 签名按 rundll32 约定: void CALLBACK f(HWND, HINSTANCE, LPSTR, int)
// (x64 只有一种调用约定, 两个指针/字符串参数直接忽略即可)
extern "C" __declspec(dllexport) void CALLBACK BassMaxConfig(HWND, HINSTANCE, LPSTR, int) {
    bassmax::RunStandaloneSettings();
}
