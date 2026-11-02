// settings_test.cpp — 验证注册表显性化 + 外部改动热加载
// 与 bassmax_settings.cpp 一起编译; 直接操作 HKCU 模拟"用户手改注册表"。
#include "bassmax_settings.h"
#include <stdio.h>
#include <windows.h>

using namespace bassmax;

static const wchar_t* kKey = L"Software\\BassMax";

static void SetSz(LPCWSTR name, LPCWSTR val) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
        RegCloseKey(hk);
    }
}
static void SetDw(LPCWSTR name, DWORD v) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hk, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }
}
static void DelVal(LPCWSTR name) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_WRITE, &hk) == ERROR_SUCCESS) {
        RegDeleteValueW(hk, name);
        RegCloseKey(hk);
    }
}
static bool HasVal(LPCWSTR name) {
    HKEY hk;
    bool has = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        has = (RegQueryValueExW(hk, name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS);
        RegCloseKey(hk);
    }
    return has;
}

static int g_pass = 0, g_fail = 0;
static void Check(bool cond, const char* what, const char* detail = "") {
    if (cond) { ++g_pass; printf("  PASS  %s\n", what); }
    else      { ++g_fail; printf("  FAIL  %s  %s\n", what, detail); }
}

int wmain() {
    printf("=== 初始状态 (InitSettings 之后) ===\n");
    Settings s = GetSettings();
    printf("  Bass=%.2f Treble=%.2f Drive=%.2f Preset=%d Enabled=%d TrayIcon=%d gen=%ld\n",
           s.bass, s.treble, s.drive, s.preset, s.enabled ? 1 : 0,
           TrayEnabled() ? 1 : 0, SettingsGen());

    printf("\n=== 1) 六项值是否都已显性写入注册表 ===\n");
    Check(HasVal(L"Bass"), "Bass 存在");
    Check(HasVal(L"Treble"), "Treble 存在");
    Check(HasVal(L"Drive"), "Drive 存在");
    Check(HasVal(L"Preset"), "Preset 存在");
    Check(HasVal(L"Enabled"), "Enabled 存在");
    Check(HasVal(L"TrayIcon"), "TrayIcon 存在");

    Settings orig = s;
    bool trayOrig = TrayEnabled();

    printf("\n=== 2) 外部改 Bass 字符串值 -> 热加载 ===\n");
    LONG g0 = SettingsGen();
    SetSz(L"Bass", L"5.5");
    LONG g1 = ReloadFromRegistry();
    Check(g1 > g0, "变更代号递增");
    Check(GetSettings().bass > 5.4f && GetSettings().bass < 5.6f, "Bass 变成 5.5");

    printf("\n=== 3) 外部用 DWORD 写 Bass -> 也应识别 ===\n");
    SetDw(L"Bass", 7);
    LONG g2 = ReloadFromRegistry();
    Check(g2 > g1, "变更代号递增");
    Check(GetSettings().bass > 6.9f && GetSettings().bass < 7.1f, "Bass 变成 7.0");

    printf("\n=== 4) 外部关闭托盘图标 TrayIcon=0 ===\n");
    SetSz(L"TrayIcon", L"0");
    ReloadFromRegistry();
    Check(!TrayEnabled(), "TrayEnabled() == false");

    printf("\n=== 5) 再打开 TrayIcon=1 ===\n");
    SetSz(L"TrayIcon", L"1");
    ReloadFromRegistry();
    Check(TrayEnabled(), "TrayEnabled() == true");

    printf("\n=== 6) 值缺失时不采纳 (避免刷成默认) ===\n");
    LONG g3 = SettingsGen();
    DelVal(L"TrayIcon");
    LONG g4 = ReloadFromRegistry();
    Check(g4 == g3, "缺值 -> 代号不变");
    Check(TrayEnabled(), "TrayEnabled 保持原值");

    printf("\n=== 7) 删掉 TrayIcon (下一个进程启动时应自动补回) ===\n");
    SetSz(L"TrayIcon", L"1");     // 先放回, 下面 DelVal 再删, 模拟首次安装后用户误删
    DelVal(L"TrayIcon");
    Check(!HasVal(L"TrayIcon"), "已删除");

    printf("\n=== 8) 超范围值被钳住 ===\n");
    SetSz(L"Bass", L"99");
    ReloadFromRegistry();
    Check(GetSettings().bass <= 12.01f, "Bass 钳到 +12");

    // 还原
    Settings r = orig;
    SetSettings(r);
    SetSz(L"TrayIcon", trayOrig ? L"1" : L"0");
    ReloadFromRegistry();
    printf("\n还原: Bass=%.1f Treble=%.1f Drive=%.2f Preset=%d Enabled=%d TrayIcon=%d\n",
           GetSettings().bass, GetSettings().treble, GetSettings().drive,
           GetSettings().preset, GetSettings().enabled ? 1 : 0, TrayEnabled() ? 1 : 0);

    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
