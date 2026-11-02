// bassmax_settings.cpp
#include "bassmax_settings.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

namespace bassmax {

static CRITICAL_SECTION s_cs;
static INIT_ONCE        s_once = INIT_ONCE_STATIC_INIT;
static Settings         s_val = { 0.0f, 0.0f, 0 };
static volatile LONG    s_gen = 0;
static bool             s_tray = true;

static const wchar_t* kKey = L"Software\\BassMax";

static float ClampBass(float v)  { if (v < -12.f) return -12.f; if (v > 12.f) return 12.f; return v; }
static float ClampDrive(float v) { if (v < 0.f) return 0.f;   if (v > 1.f) return 1.f;   return v; }
static int   ClampPreset(int v)  { if (v < 0) return 0; if (v > 5) return 5; return v; }

static BOOL CALLBACK InitOnce(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_cs);

    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        WCHAR buf[64];
        DWORD sz = sizeof(buf), type = 0;
        if (RegQueryValueExW(hk, L"Bass", 0, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS) {
            buf[(sizeof(buf) / sizeof(buf[0])) - 1] = 0;
            s_val.bass = (float)(float)wcstod(buf, nullptr);
        }
        sz = sizeof(buf); type = 0;
        if (RegQueryValueExW(hk, L"Drive", 0, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS) {
            buf[(sizeof(buf) / sizeof(buf[0])) - 1] = 0;
            s_val.drive = (float)(float)wcstod(buf, nullptr);
        }
        sz = sizeof(buf); type = 0;
        if (RegQueryValueExW(hk, L"Preset", 0, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS) {
            buf[(sizeof(buf) / sizeof(buf[0])) - 1] = 0;
            s_val.preset = (int)wcstod(buf, nullptr);
        }
        sz = sizeof(buf); type = 0;
        if (RegQueryValueExW(hk, L"TrayIcon", 0, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS) {
            buf[(sizeof(buf) / sizeof(buf[0])) - 1] = 0;
            s_tray = ((float)wcstod(buf, nullptr) != 0.0);
        }
        RegCloseKey(hk);
    }
    s_val.bass   = ClampBass(s_val.bass);
    s_val.drive  = ClampDrive(s_val.drive);
    s_val.preset = ClampPreset(s_val.preset);

    // DspVer < 2: "驱动" 的含义从"推入限幅器"改成"动态增强量", 旧值直接沿用会过冲, 迁移到温和默认值
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        WCHAR buf[64]; DWORD sz = sizeof(buf), type = 0;
        int ver = 0;
        if (RegQueryValueExW(hk, L"DspVer", 0, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS) {
            buf[(sizeof(buf)/sizeof(buf[0])) - 1] = 0;
            ver = (int)wcstod(buf, nullptr);
        }
        // DspVer < 3: 驱动含义改成「电平拉伸量」, 旧值直接沿用会立刻变响很多 -> 归零,
        // 让用户自己从 0 往上推 (0 = 完全无效果, 与旧版行为对齐)。
        if (ver < 3) {
            s_val.drive  = 0.0f;
            s_val.preset = 0;
            WCHAR tmp[32];
            swprintf_s(tmp, L"3");   RegSetValueExW(hk, L"DspVer", 0, REG_SZ, (const BYTE*)tmp, (DWORD)((wcslen(tmp)+1)*sizeof(WCHAR)));
            swprintf_s(tmp, L"%.2f", s_val.drive); RegSetValueExW(hk, L"Drive", 0, REG_SZ, (const BYTE*)tmp, (DWORD)((wcslen(tmp)+1)*sizeof(WCHAR)));
            swprintf_s(tmp, L"0");   RegSetValueExW(hk, L"Preset", 0, REG_SZ, (const BYTE*)tmp, (DWORD)((wcslen(tmp)+1)*sizeof(WCHAR)));
        }
        RegCloseKey(hk);
    }
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
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;
    WCHAR buf[64];
    swprintf_s(buf, L"%.1f", s.bass);
    RegSetValueExW(hk, L"Bass", 0, REG_SZ, (const BYTE*)buf, (DWORD)((wcslen(buf) + 1) * sizeof(WCHAR)));
    swprintf_s(buf, L"%.2f", s.drive);
    RegSetValueExW(hk, L"Drive", 0, REG_SZ, (const BYTE*)buf, (DWORD)((wcslen(buf) + 1) * sizeof(WCHAR)));
    swprintf_s(buf, L"%d", s.preset);
    RegSetValueExW(hk, L"Preset", 0, REG_SZ, (const BYTE*)buf, (DWORD)((wcslen(buf) + 1) * sizeof(WCHAR)));
    RegCloseKey(hk);
}

LONG SetSettings(const Settings& in) {
    InitSettings();
    Settings s;
    s.bass   = ClampBass(in.bass);
    s.drive  = ClampDrive(in.drive);
    s.preset = ClampPreset(in.preset);
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
    return s_tray;
}

} // namespace bassmax
