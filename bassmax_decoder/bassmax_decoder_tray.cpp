// bassmax_decoder_tray.cpp — bassmax_decoder.ax 进程内托盘 (单击 = 总开关)
//
// 为什么可以这样加, 而不会撞上"属性页冲突":
//   属性页冲突是 COM 层的 CLSID_BassMaxPage (...C9B2) 的 InprocServer32 被两张
//   .ax 抢注。托盘只是 Shell_NotifyIcon + 一个隐藏窗口 + 一个线程, 不注册任何
//   CLSID, 不实现 ISpecifyPropertyPages, 不导出符号, 不走 COM 激活。两者零交集。
//   旧 bassmax_dshow.ax 本来就是这么做的 (StartTray 在滤镜构造里), 只是新壳当初
//   连托盘一起砍了。现在补回来。
//
// 生效链路 (比外部程序更快):
//   单击 -> bassmax::SetSettings() 写注册表 + 递增本 DLL 的 s_gen
//        -> DeliverPcm 里 m_dsp.Tick() 下一拍就 ApplySettings()
//        -> enabled=false 时参数全归零, Core 逐样本 bypass
//   同一进程内没有跨 DLL 的 s_gen 隔离问题, 不用等 400ms 回读。
//   400ms timer 只用来同步外部改动 (手改注册表 / 旧设置窗口) 和图标状态。
//
// 生命周期 (照搬 bassmax_dshow/bassmax_tray.cpp 已验证的方案):
//   * 每进程只开一个托盘线程, 活到进程结束 (DllCanUnloadNow 恒 S_FALSE, DLL 不卸载)
//   * AddRef/Release 只增减引用 + PostMessage, 绝不 WaitForSingleObject ——
//     BmdFilter 析构可能跑在播放器拆图/切音轨线程上, 阻塞一毫秒都是画面卡顿
//   * 引用归零只隐藏图标, 线程不退出; 下次播放直接复用
//
// 交互: 左键单击 / 右键单击 都 = 开启/关闭。
//       与 bassmax_dshow.ax 的"右键切总开关"对齐, 同时保留新壳原有的左键开关,
//       两个键结果一致, 不会出现"按了没反应"。decoder 壳没有设置对话框资源
//       (当初为躲属性页冲突砍的), 所以不像旧 .ax 那样把左键绑到设置窗口;
//       也不做右键菜单/退出项 —— 进程内托盘生命周期跟滤镜走, 播放停止图标
//       自然消失, 没有"退出"概念。
//       注意: 只认 *BUTTONUP, 不额外处理 WM_CONTEXTMENU, 否则某些 Windows
//       版本会两个消息都发, 一次右键变成翻转两次 (看起来像没反应)。
// 图标: 内嵌 IDI_DECODER_ON / IDI_DECODER_OFF; 若 .ax 同目录放了
//       bassmax.ico / bassmax_off.ico, 优先用文件里的 (换图标不用重编译)。
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "bassmax_decoder_tray.h"
#include "bassmax_settings.h"
#include "resource.h"

// bassmax_decoder.cpp 的 DllMain 里已经设好; 这里只借用模块句柄加载图标资源。
extern HINSTANCE g_hInst;

namespace {

const UINT      kTrayMsg  = WM_APP + 101;
const UINT      kWakeMsg  = WM_APP + 102;   // 引用数变化 -> 重算显隐
const UINT_PTR  kTimerId  = 1;             // 400ms 同步外部改动
const UINT      kTrayId   = 1;
const wchar_t*  kWndClass = L"BassMaxDecoderTrayWnd";  // 与旧 .ax 的 BassMaxTrayWnd 分开

// ---- 诊断日志: 与 bassmax_decoder.cpp 的 BLog 写同一个文件 ----
void TLog(const char* fmt, ...) {
    char path[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "bassmax_decoder.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    fprintf(f, "%8lu [%5lu] [TRAY] ",
            (unsigned long)GetTickCount(), (unsigned long)GetCurrentThreadId());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---- 进程内全局状态 ----
INIT_ONCE        s_once = INIT_ONCE_STATIC_INIT;
CRITICAL_SECTION s_cs;
LONG             s_ref     = 0;      // 活着的 BmdFilter 实例数 (s_cs 保护)
HANDLE           s_thread  = nullptr;
HWND             s_trayWnd = nullptr;

// 以下只在托盘线程访问
NOTIFYICONDATAW  s_nid    = {};
HICON            s_icoOn  = nullptr;
HICON            s_icoOff = nullptr;
bool             s_shown  = false;
int              s_drawn  = -1;
UINT             s_taskbarCreated = 0;
LONG             s_lastGen = -1;

BOOL CALLBACK InitCs(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_cs);
    return TRUE;
}
void EnsureCs() { InitOnceExecuteOnce(&s_once, InitCs, nullptr, nullptr); }

LONG TrayRef() {
    EnsureCs();
    EnterCriticalSection(&s_cs);
    LONG r = s_ref;
    LeaveCriticalSection(&s_cs);
    return r;
}

HMODULE ModuleHandle() {
    if (g_hInst) return (HMODULE)g_hInst;
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void*)&ModuleHandle, &h);
    return h;
}

HICON LoadIconSmart(int resId, const wchar_t* resName, const wchar_t* fileName) {
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    HMODULE mod = ModuleHandle();

    // 1) .ax 同目录的同名 ico 文件优先 (换图标不用重编译)
    wchar_t path[MAX_PATH] = {};
    if (mod) {
        GetModuleFileNameW(mod, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) {
            slash[1] = 0;
            wcsncat_s(path, MAX_PATH, fileName, _TRUNCATE);
            HICON h = (HICON)LoadImageW(nullptr, path, IMAGE_ICON, cx, cy,
                                        LR_LOADFROMFILE | LR_DEFAULTCOLOR);
            if (h) { TLog("icon[%d] from file: %ls", resId, path); return h; }
        }
    }
    if (!mod) return nullptr;

    // 2) 整数 ID —— 正常路径
    HICON h = (HICON)LoadImageW(mod, MAKEINTRESOURCEW(resId), IMAGE_ICON,
                                cx, cy, LR_DEFAULTCOLOR);
    if (h) { TLog("icon[%d] from resource id", resId); return h; }

    // 3) 兜底: rc.exe 若没展开宏, 资源名会被注册成字符串。以前这里静默退回
    //    系统默认图标, 结果"开启"显示成 Windows 空白图标、"关闭"却正常,
    //    极难排查。现在先按字符串名找, 再不行也明确打日志。
    h = (HICON)LoadImageW(mod, resName, IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (h) { TLog("icon[%d] from resource name %ls", resId, resName); return h; }

    TLog("icon[%d] LOAD FAILED (id=%d name=%ls)", resId, resId, resName);
    return nullptr;
}

void SetTip(bool on) {
    _snwprintf_s(s_nid.szTip, sizeof(s_nid.szTip) / sizeof(wchar_t), _TRUNCATE,
                  L"BassMax · %s (左/右键单击切换)", on ? L"已开启" : L"已关闭");
}

void ShowIcon(bool on) {
    if (!s_nid.hWnd || s_shown) return;
    s_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    HICON h = on ? s_icoOn : s_icoOff;
    if (h) s_nid.hIcon = h;
    SetTip(on);
    if (Shell_NotifyIconW(NIM_ADD, &s_nid)) {
        s_shown = true; s_drawn = (int)on;
        TLog("NIM_ADD ok enabled=%d", (int)on);
    } else {
        s_shown = false; s_drawn = -1;
        TLog("NIM_ADD failed (Explorer 未就绪?) 下一拍重试");
    }
}

void HideIcon() {
    if (!s_nid.hWnd || !s_shown) return;
    Shell_NotifyIconW(NIM_DELETE, &s_nid);
    s_shown = false; s_drawn = -1;
    TLog("NIM_DELETE");
}

// 已显示时只刷开关状态; 状态没变就不动, 避免每 400ms 无谓刷新
void SyncIcon(bool on, bool force = false) {
    if (!s_nid.hWnd || !s_shown) return;
    if (s_drawn == (int)on && !force) return;
    s_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    HICON h = on ? s_icoOn : s_icoOff;
    if (h) s_nid.hIcon = h;
    SetTip(on);
    if (Shell_NotifyIconW(NIM_MODIFY, &s_nid)) s_drawn = (int)on;
    else { s_shown = false; s_drawn = -1; }
}

void UpdateVisibility() {
    if (!s_nid.hWnd) return;
    // 有滤镜实例 + TrayIcon 不为 0 -> 显示。TrayIcon 沿用旧 .ax 的语义,
    // 设 0 可一次性关掉所有 BassMax 托盘。
    if (TrayRef() > 0 && bassmax::TrayEnabled()) {
        bool on = bassmax::GetSettings().enabled;
        if (!s_shown) ShowIcon(on);
        else          SyncIcon(on);
    } else {
        HideIcon();
    }
}

// 单击 (左/右键都走这里): 直接翻转本 DLL 的设置。SetSettings 会写注册表并
// 递增 s_gen, 音频线程下一拍 Tick() 就重建系数, 播放中立即生效。
void Toggle(const wchar_t* button) {
    bassmax::Settings s = bassmax::GetSettings();
    s.enabled = !s.enabled;
    s_lastGen = bassmax::SetSettings(s);
    TLog("%ls CLICK -> enabled=%d gen=%ld", button, (int)s.enabled, (long)s_lastGen);
    SyncIcon(s.enabled, true);
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case kTrayMsg: {
        const WORD btn = LOWORD(l);
        if (btn == WM_LBUTTONUP)      Toggle(L"LEFT");    // 新壳原有习惯
        else if (btn == WM_RBUTTONUP) Toggle(L"RIGHT");   // 对齐 bassmax_dshow.ax
        return 0;
    }
    case kWakeMsg:
        UpdateVisibility();
        return 0;
    case WM_TIMER:
        if (w == kTimerId) {
            // 外部改动 (手改注册表 / 旧设置窗口 / 属性页) 同步回本 DLL 与图标
            LONG gen = bassmax::ReloadFromRegistry();
            if (gen != s_lastGen) {
                s_lastGen = gen;
                TLog("registry change gen=%ld enabled=%d",
                     (long)gen, (int)bassmax::GetSettings().enabled);
            }
            UpdateVisibility();                   // 顺带重试失败的 NIM_ADD
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    // 资源管理器重启后所有托盘图标失效, 收到广播重新挂上
    if (s_taskbarCreated && msg == s_taskbarCreated) {
        s_shown = false; s_drawn = -1;
        bool on = bassmax::GetSettings().enabled;
        if (TrayRef() > 0 && bassmax::TrayEnabled()) ShowIcon(on);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

DWORD WINAPI TrayThread(LPVOID) {
    TLog("thread start");
    HMODULE mod = ModuleHandle();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = TrayWndProc;
    wc.hInstance     = mod;
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) { TLog("RegisterClassExW failed"); return 0; }

    // 隐藏顶层窗口 (不加 WS_VISIBLE)。不用 HWND_MESSAGE: 那样收不到
    // TaskbarCreated 广播, explorer 重启后图标就回不来了。
    HWND hwnd = CreateWindowExW(0, kWndClass, L"", 0, 0, 0, 0, 0,
                                nullptr, nullptr, mod, nullptr);
    if (!hwnd) { TLog("CreateWindowExW failed"); return 0; }

    EnsureCs();
    EnterCriticalSection(&s_cs);
    s_trayWnd = hwnd;
    LeaveCriticalSection(&s_cs);

    s_icoOn  = LoadIconSmart(IDI_DECODER_ON,  L"IDI_DECODER_ON",  L"bassmax.ico");
    s_icoOff = LoadIconSmart(IDI_DECODER_OFF, L"IDI_DECODER_OFF", L"bassmax_off.ico");
    if (!s_icoOn || !s_icoOff) {
        // 最后手段, 且必须告警。以前静默退回默认图标, 结果"开启"变成
        // Windows 空白图标、"关闭"却正常, 排查起来非常费劲。
        TLog("WARNING: icon load failed -> fallback to system default");
        if (!s_icoOff) s_icoOff = s_icoOn;
        if (!s_icoOn)  s_icoOn  = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
        if (!s_icoOff)  s_icoOff = s_icoOn;
    }
    TLog("icons on=%p off=%p", s_icoOn, s_icoOff);

    s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    s_lastGen = bassmax::SettingsGen();

    s_nid = {};
    s_nid.cbSize           = sizeof(s_nid);
    s_nid.hWnd             = hwnd;
    s_nid.uID              = kTrayId;
    s_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = kTrayMsg;
    s_nid.hIcon            = s_icoOn;

    UpdateVisibility();
    SetTimer(hwnd, kTimerId, 400, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    KillTimer(hwnd, kTimerId);
    HideIcon();
    if (s_icoOn)  DestroyIcon(s_icoOn);
    if (s_icoOff && s_icoOff != s_icoOn) DestroyIcon(s_icoOff);
    s_icoOn = s_icoOff = nullptr;
    s_nid = {};
    TLog("thread exit");
    return 0;
}

} // namespace

namespace bassmax {

void DecoderTrayAddRef() {
    EnsureCs();
    HANDLE need = nullptr;
    HWND   wake = nullptr;
    EnterCriticalSection(&s_cs);
    ++s_ref;
    if (!s_thread) {
        DWORD tid = 0;
        need = CreateThread(nullptr, 0, TrayThread, nullptr, 0, &tid);
        if (need) s_thread = need;
    } else {
        wake = s_trayWnd;
    }
    LONG r = s_ref;
    LeaveCriticalSection(&s_cs);

    if (!need && wake) PostMessageW(wake, kWakeMsg, 0, 0);
    TLog("AddRef ref=%ld created=%d", (long)r, need ? 1 : 0);
    // 新建线程那条路径不用 post: 线程起来自己会 UpdateVisibility()
}

void DecoderTrayRelease() {
    EnsureCs();
    bool idle = false;
    HWND  wake = nullptr;
    EnterCriticalSection(&s_cs);
    if (s_ref > 0 && --s_ref == 0) idle = true;
    wake = s_trayWnd;
    LONG r = s_ref;
    LeaveCriticalSection(&s_cs);

    // 只发消息, 不等待、不 CloseHandle、不销毁线程 —— 析构可能跑在拆图线程上
    if (idle && wake) PostMessageW(wake, kWakeMsg, 0, 0);
    TLog("Release ref=%ld idle=%d", (long)r, idle ? 1 : 0);
}

} // namespace bassmax
