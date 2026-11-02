// bassmax_tray.cpp  —  托盘图标线程 + 设置窗口
#include "bassmax_tray.h"
#include "bassmax_ui.h"
#include "resource.h"
#include <shellapi.h>

extern HINSTANCE g_hInst;

namespace {

const UINT      kTrayMsg  = WM_APP + 101;
const UINT      kTrayId   = 1;
const wchar_t*  kWndClass = L"BassMaxTrayWnd";

CRITICAL_SECTION g_cs;
bool             g_csInit = false;
int              g_ref    = 0;
HANDLE           g_thread = nullptr;
HWND             g_trayWnd = nullptr;   // 托盘线程里的隐藏窗口
HWND             g_dlg     = nullptr;   // 设置窗口 (在托盘线程创建)

void EnsureCs() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }

INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM w, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        bassmax::UiLoadFromSettings(hwnd);
        return TRUE;
    case WM_HSCROLL:
        bassmax::UiUpdateLabels(hwnd);
        bassmax::SetSettings(bassmax::UiReadSettings(hwnd));   // 实时生效并保存
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) { DestroyWindow(hwnd); return TRUE; }
        if (id == IDC_CHK_ON) {                       // 总开关: 立即生效
            bassmax::SetSettings(bassmax::UiReadSettings(hwnd));
            bassmax::UiUpdateLabels(hwnd);
            return TRUE;
        }
        if (bassmax::UiPresetClicked(hwnd, id) >= 0) {
            bassmax::SetSettings(bassmax::UiReadSettings(hwnd));   // 换风格立即生效
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
        if (LOWORD(l) == WM_LBUTTONDBLCLK || LOWORD(l) == WM_RBUTTONUP)
            bassmax::ShowTraySettings();
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

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));   // IDI_APPLICATION
    wcscpy_s(nid.szTip, L"BassMax 音频增强 (双击打开设置)");
    Shell_NotifyIconW(NIM_ADD, &nid);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
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

void ShowTraySettings() {
    if (g_dlg) { ShowWindow(g_dlg, SW_SHOW); SetForegroundWindow(g_dlg); return; }
    g_dlg = CreateDialogW(g_hInst, MAKEINTRESOURCEW(IDD_BASSMAX_POPUP), nullptr, SettingsDlgProc);
    if (g_dlg) ShowWindow(g_dlg, SW_SHOW);
}

} // namespace bassmax
