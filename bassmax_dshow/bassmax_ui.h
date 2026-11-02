// bassmax_ui.h  —  属性页与托盘设置窗口共用的控件读写
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "bassmax_settings.h"
#include "resource.h"

namespace bassmax {

// 低音: 滑块 0..240 -> -12.0 .. +12.0 dB
// 驱动: 滑块 0..10  -> 0 / 10 / ... / 100 %   (每级一个固定值)
inline Settings UiReadSettings(HWND hwnd) {
    Settings s;
    int b = (int)SendDlgItemMessageW(hwnd, IDC_SLD_BASS,  TBM_GETPOS, 0, 0);
    int d = (int)SendDlgItemMessageW(hwnd, IDC_SLD_DRIVE, TBM_GETPOS, 0, 0);
    s.bass  = (b - 120) / 10.0f;
    s.drive = d / 10.0f;
    s.preset = 0;
    for (int i = 0; i < 6; ++i) {
        if (IsDlgButtonChecked(hwnd, IDC_PRE0 + i) == BST_CHECKED) { s.preset = i; break; }
    }
    return s;
}

inline void UiUpdateLabels(HWND hwnd) {
    Settings s = UiReadSettings(hwnd);
    wchar_t buf[32];
    swprintf_s(buf, L"%+.1f dB", s.bass);
    SetDlgItemTextW(hwnd, IDC_TXT_BASS, buf);
    swprintf_s(buf, L"%d %%", (int)(s.drive * 100.f + 0.5f));
    SetDlgItemTextW(hwnd, IDC_TXT_DRIVE, buf);
}

inline void UiLoadFromSettings(HWND hwnd) {
    Settings s = GetSettings();
    SendDlgItemMessageW(hwnd, IDC_SLD_BASS,  TBM_SETRANGE, TRUE, MAKELPARAM(0, 240));
    SendDlgItemMessageW(hwnd, IDC_SLD_DRIVE, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10));
    SendDlgItemMessageW(hwnd, IDC_SLD_BASS,  TBM_SETPOS, TRUE, (LPARAM)(int)((s.bass + 12.f) * 10.f + 0.5f));
    SendDlgItemMessageW(hwnd, IDC_SLD_DRIVE, TBM_SETPOS, TRUE, (LPARAM)(int)(s.drive * 10.f + 0.5f));
    for (int i = 0; i < 6; ++i)
        CheckDlgButton(hwnd, IDC_PRE0 + i, (s.preset == i) ? BST_CHECKED : BST_UNCHECKED);
    UiUpdateLabels(hwnd);
}

// 点击预设按钮后重新勾选 (单选按钮组在对话框里会自动取消其它项, 这里只做兜底)
inline int UiPresetClicked(HWND hwnd, int id) {
    if (id < IDC_PRE0 || id > IDC_PRE5) return -1;
    for (int i = 0; i < 6; ++i)
        CheckDlgButton(hwnd, IDC_PRE0 + i, (IDC_PRE0 + i == id) ? BST_CHECKED : BST_UNCHECKED);
    return id - IDC_PRE0;
}

} // namespace bassmax
