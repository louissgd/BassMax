// bassmax_settings.cpp  —  注册表配置的读取 / 写入 / 外部改动热加载
#include "bassmax_settings.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

namespace bassmax {

static CRITICAL_SECTION s_cs;
static INIT_ONCE        s_once = INIT_ONCE_STATIC_INIT;
static Settings         s_val = { 2.0f, 2.0f, 1.0f, 4, true };   // 默认: 爵士 + 低音+2 + 高音+2 + 驱动100%
static volatile LONG    s_gen = 0;
static volatile LONG    s_tray = 1;

static const wchar_t* kKey = L"Software\\BassMax";

static float ClampBass(float v)  { if (v < -12.f) return -12.f; if (v > 12.f) return 12.f; return v; }
static float ClampDrive(float v) { if (v < 0.f) return 0.f;   if (v > 1.f) return 1.f;   return v; }
static int   ClampPreset(int v)  { if (v < 0) return 0; if (v > 5) return 5; return v; }

// 读一个数值: REG_SZ 与 REG_DWORD 都接受。
// 手改注册表时不管新建"字符串值"还是"DWORD 值"都能生效, 不必关心类型。
static bool ReadNum(HKEY hk, LPCWSTR name, double* out) {
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(hk, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS) return false;

    if (type == REG_DWORD) {
        DWORD v = 0, n = sizeof(v);
        if (RegQueryValueExW(hk, name, nullptr, &type, (BYTE*)&v, &n) != ERROR_SUCCESS) return false;
        *out = (double)v;
        return true;
    }
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        if (cb > 512) cb = 512;
        WCHAR buf[256] = {};
        DWORD n = cb;
        if (RegQueryValueExW(hk, name, nullptr, &type, (BYTE*)buf, &n) != ERROR_SUCCESS) return false;
        buf[255] = 0;
        *out = wcstod(buf, nullptr);
        return true;
    }
    return false;
}

static void WriteStr(HKEY hk, LPCWSTR name, LPCWSTR val) {
    RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val,
                   (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
}

// 把整套参数 (含 TrayIcon) 写成字符串值
static void SaveTo(HKEY hk, const Settings& s, bool tray) {
    WCHAR buf[64];
    swprintf_s(buf, L"%.1f", s.bass);              WriteStr(hk, L"Bass", buf);
    swprintf_s(buf, L"%.1f", s.treble);            WriteStr(hk, L"Treble", buf);
    swprintf_s(buf, L"%.2f", s.drive);             WriteStr(hk, L"Drive", buf);
    swprintf_s(buf, L"%d",   s.preset);            WriteStr(hk, L"Preset", buf);
    swprintf_s(buf, L"%d",   s.enabled ? 1 : 0);   WriteStr(hk, L"Enabled", buf);
    swprintf_s(buf, L"%d",   tray ? 1 : 0);        WriteStr(hk, L"TrayIcon", buf);
}

// 逐项读取 (缺哪项就留默认值), 返回是否全部读到
static bool ReadAll(HKEY hk, Settings& s, bool& tray) {
    bool ok = true;
    double d = 0;
    if (ReadNum(hk, L"Bass", &d))     s.bass   = ClampBass((float)d);   else ok = false;
    if (ReadNum(hk, L"Treble", &d))   s.treble = ClampBass((float)d);   else ok = false;
    if (ReadNum(hk, L"Drive", &d))    s.drive  = ClampDrive((float)d);  else ok = false;
    if (ReadNum(hk, L"Preset", &d))   s.preset = ClampPreset((int)d);   else ok = false;
    if (ReadNum(hk, L"Enabled", &d))  s.enabled = (d != 0.0);           else ok = false;
    if (ReadNum(hk, L"TrayIcon", &d)) tray       = (d != 0.0);          else ok = false;
    return ok;
}

static BOOL CALLBACK InitOnce(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_cs);

    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS) {
        InterlockedIncrement(&s_gen);
        return TRUE;
    }

    Settings s   = s_val;
    bool     tray = (s_tray != 0);
    bool complete = ReadAll(hk, s, tray);

    // 配置版本迁移 (与旧版一致, 不动)
    double d = 0;
    int ver = ReadNum(hk, L"DspVer", &d) ? (int)d : 0;

    // DspVer < 3: 驱动含义改成「电平拉伸量」, 旧值沿用会立刻变响很多 -> 归零
    if (ver < 3) {
        s.drive  = 0.0f;
        s.preset = 0;
        WriteStr(hk, L"DspVer", L"3");
        complete = false;
    }
    // DspVer < 4: 定稿默认值「爵士 + 低音 +2 + 高音 +2 + 驱动 100%」, 一次性套用
    if (ver < 4) {
        s.bass   = 2.0f;
        s.treble = 2.0f;
        s.drive  = 1.0f;
        s.preset = 4;
        WriteStr(hk, L"DspVer", L"4");
        complete = false;
    }

    s_val  = s;
    s_tray = tray ? 1 : 0;

    // 缺值就补齐: 首次运行后注册表里一定有完整的 Bass/Treble/Drive/Preset/
    // Enabled/TrayIcon 六项, 用户可以直接照着改, 不用猜值名。
    if (!complete) SaveTo(hk, s_val, s_tray != 0);

    RegCloseKey(hk);
    InterlockedIncrement(&s_gen);
    return TRUE;
}

void InitSettings() {
    InitOnceExecuteOnce(&s_once, InitOnce, nullptr, nullptr);
}

Settings GetSettings() {
    InitSettings();
    EnterCriticalSection(&s_cs);
    Settings s = s_val;
    LeaveCriticalSection(&s_cs);
    return s;
}

static void SaveLocked(const Settings& s) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;
    SaveTo(hk, s, s_tray != 0);
    RegCloseKey(hk);
}

LONG SetSettings(const Settings& in) {
    InitSettings();
    Settings s;
    s.bass    = ClampBass(in.bass);
    s.treble  = ClampBass(in.treble);
    s.drive   = ClampDrive(in.drive);
    s.preset  = ClampPreset(in.preset);
    s.enabled = in.enabled;
    EnterCriticalSection(&s_cs);
    s_val = s;
    SaveLocked(s);
    LONG g = InterlockedIncrement(&s_gen);
    LeaveCriticalSection(&s_cs);
    return g;
}

LONG SettingsGen() {
    InitSettings();
    return s_gen;
}

bool TrayEnabled() {
    InitSettings();
    return s_tray != 0;
}

LONG ReloadFromRegistry() {
    InitSettings();

    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return SettingsGen();

    EnterCriticalSection(&s_cs);
    Settings t = s_val;
    bool     tray = (s_tray != 0);
    LeaveCriticalSection(&s_cs);

    bool ok = ReadAll(hk, t, tray);
    RegCloseKey(hk);
    if (!ok) return SettingsGen();          // 值不全: 用户可能正改到一半, 本次不采纳

    EnterCriticalSection(&s_cs);
    bool changed = (t.bass    != s_val.bass)   ||
                   (t.treble  != s_val.treble) ||
                   (t.drive   != s_val.drive)  ||
                   (t.preset  != s_val.preset) ||
                   (t.enabled != s_val.enabled)||
                   (tray      != (s_tray != 0));
    if (changed) {
        s_val  = t;
        s_tray = tray ? 1 : 0;
        InterlockedIncrement(&s_gen);
    }
    LONG g = s_gen;
    LeaveCriticalSection(&s_cs);
    return g;
}

} // namespace bassmax
