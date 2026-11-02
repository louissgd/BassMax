// config_main.cpp — BassMaxConfig.exe
// 用途: 播放器不启动时也能打开 BassMax 设置窗口。
// 做法: 找到 bassmax_dshow.ax -> LoadLibrary -> 调它的导出 BassMaxConfig。
//       (UI 和参数存储都在 .ax 里, 这里只做定位 + 唤起, 不重复实现一份界面)
#include <windows.h>
#include <stdio.h>

static const wchar_t* kAxName = L"bassmax_dshow.ax";
static const wchar_t* kClsid  = L"{C41F7D22-6B18-4A93-8E52-1D7A4F60C9B1}";   // CLSID_BassMax

static bool Exists(LPCWSTR p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void DirOfSelf(LPWSTR out, DWORD cch) {
    DWORD n = GetModuleFileNameW(nullptr, out, cch);
    if (!n || n >= cch) { out[0] = 0; return; }
    WCHAR* p = wcsrchr(out, L'\\');
    if (p) *p = 0; else out[0] = 0;
}

// 注册表里登记的滤镜路径 (install.bat / regsvr32 写进去的) 作为最后兜底
static bool FromRegistry(LPWSTR out, DWORD cch) {
    WCHAR key[128];
    swprintf_s(key, L"CLSID\\%s\\InprocServer32", kClsid);
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &hk) != ERROR_SUCCESS) return false;
    DWORD cb = cch * sizeof(WCHAR), type = 0;
    LONG r = RegQueryValueExW(hk, nullptr, nullptr, &type, (BYTE*)out, &cb);
    RegCloseKey(hk);
    return r == ERROR_SUCCESS && out[0] != 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    WCHAR ax[MAX_PATH] = {};

    WCHAR dir[MAX_PATH] = {};
    DirOfSelf(dir, MAX_PATH);
    if (dir[0]) swprintf_s(ax, L"%s\\%s", dir, kAxName);

    if (!ax[0] || !Exists(ax)) {
        bool found = false;

        WCHAR prof[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
        if (n && n < MAX_PATH) {
            swprintf_s(ax, L"%s\\MyMPC\\Codecs\\%s", prof, kAxName);
            found = Exists(ax);
        }
        if (!found && FromRegistry(ax, MAX_PATH) && Exists(ax)) found = true;

        if (!found) {
            MessageBoxW(nullptr,
                L"找不到 bassmax_dshow.ax。\n\n"
                L"请把 BassMaxConfig.exe 与 bassmax_dshow.ax 放在同一目录，\n"
                L"或先运行 install.bat 注册滤镜后再试。",
                L"BassMax 设置", MB_ICONERROR | MB_OK);
            return 1;
        }
    }

    HMODULE h = LoadLibraryW(ax);
    if (!h) {
        WCHAR msg[600];
        swprintf_s(msg, L"无法加载滤镜模块：\n%s\n\n系统错误码：%lu", ax, GetLastError());
        MessageBoxW(nullptr, msg, L"BassMax 设置", MB_ICONERROR | MB_OK);
        return 2;
    }

    typedef void (CALLBACK *PFN)(HWND, HINSTANCE, LPWSTR, int);
    PFN fn = (PFN)GetProcAddress(h, "BassMaxConfig");
    if (!fn) {
        MessageBoxW(nullptr,
            L"这个 bassmax_dshow.ax 版本太旧，没有独立设置入口。\n请换用与 BassMaxConfig.exe 同批发布的 .ax。",
            L"BassMax 设置", MB_ICONERROR | MB_OK);
        FreeLibrary(h);
        return 3;
    }

    fn(nullptr, nullptr, nullptr, SW_SHOWNORMAL);
    FreeLibrary(h);
    return 0;
}
