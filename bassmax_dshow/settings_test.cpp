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

// InitSettings 每个进程只跑一次 (INIT_ONCE), 所以要验证"启动时的读写行为",
// 只能另起一个进程。子进程把结果写到临时文件, 父进程读回来。
static void ProbePath(wchar_t* out, DWORD cch) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    swprintf_s(out, cch, L"%lsbassmax_probe.txt", dir);
}

static bool RunProbe(float* bass, float* treble, float* drive, int* preset, int* tray) {
    wchar_t exe[MAX_PATH] = {}, cmd[MAX_PATH + 32] = {}, out[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    swprintf_s(cmd, L"\"%ls\" --probe", exe);
    ProbePath(out, MAX_PATH);

    STARTUPINFOW        si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) return false;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, out, L"r") != 0 || !fp) return false;
    int n = fwscanf_s(fp, L"%f %f %f %d %d", bass, treble, drive, preset, tray);
    fclose(fp);
    DeleteFileW(out);
    return n == 5;
}

int wmain(int argc, wchar_t** argv) {
    // ---- 子进程模式: 只做一次 InitSettings, 把结果写到临时文件 ----
    if (argc > 1 && wcscmp(argv[1], L"--probe") == 0) {
        Settings s = GetSettings();
        wchar_t out[MAX_PATH] = {};
        ProbePath(out, MAX_PATH);
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, out, L"w") != 0 || !fp) return 2;
        fwprintf(fp, L"%.4f %.4f %.4f %d %d\n",
                 s.bass, s.treble, s.drive, s.preset, TrayEnabled() ? 1 : 0);
        fclose(fp);
        return 0;
    }

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

    // ---- 9) 1.0.1 起取消 DspVer 迁移: 旧标记 + 自定义参数必须原样保留 ----
    // 旧实现里 DspVer 只在迁移时写入, 一旦丢失 (<4) 就会把参数整套覆盖成默认值。
    // 这里把 DspVer 故意写成老值 1, 再看下一个进程读到的是什么。
    printf("\n=== 9) 遗留 DspVer + 自定义参数 -> 不得被改写 ===\n");
    SetSz(L"Bass", L"-5.0");
    SetSz(L"Treble", L"7.5");
    SetSz(L"Drive", L"0.35");
    SetSz(L"Preset", L"1");
    SetSz(L"DspVer", L"1");                  // 旧版语义标记, 值故意设成老的
    float pb = 0, pt = 0, pd = 0; int pp = -1, ptray = -1;
    bool probed = RunProbe(&pb, &pt, &pd, &pp, &ptray);
    Check(probed, "子进程 InitSettings 跑通");
    if (probed) {
        Check(pb > -5.02f && pb < -4.98f, "Bass 保持 -5.0");
        Check(pt > 7.48f && pt < 7.52f, "Treble 保持 7.5");
        Check(pd > 0.34f && pd < 0.36f, "Drive 保持 0.35 (旧版会归零)");
        Check(pp == 1, "Preset 保持 1 (旧版会改成 4)");
    }
    Check(!HasVal(L"DspVer"), "DspVer 已被清掉, 注册表无遗留项");

    printf("\n=== 10) 全新安装 (整键删除) -> 落到 1.0.1 默认值 ===\n");
    {
        HKEY hk = nullptr;
        if (RegDeleteTreeW(HKEY_CURRENT_USER, kKey) == ERROR_SUCCESS) {
            float nb = 0, nt = 0, nd = 0; int np = -1, ntray = -1;
            bool p2 = RunProbe(&nb, &nt, &nd, &np, &ntray);
            Check(p2, "清空后子进程跑通");
            if (p2) {
                Check(nb > 1.98f && nb < 2.02f, "Bass 默认 +2.0");
                Check(nt > 1.98f && nt < 2.02f, "Treble 默认 +2.0");
                Check(nd > 0.99f && nd < 1.01f, "Drive 默认 1.00");
                Check(np == 4, "Preset 默认 4 (爵士)");
                Check(ntray == 1, "TrayIcon 默认 1");
            }
            Check(HasVal(L"Bass") && HasVal(L"Treble") && HasVal(L"Drive") &&
                  HasVal(L"Preset") && HasVal(L"Enabled") && HasVal(L"TrayIcon"),
                  "六项值已显性写回");
        } else {
            Check(false, "删除注册表键失败");
        }
    }

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
