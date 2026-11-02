// test_reg.cpp — 验证 DllRegisterServer / DllUnregisterServer 写/删注册表
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

static const GUID CLSID_BassMaxT = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1}
};
typedef HRESULT (STDAPICALLTYPE *pfnReg)();

int main() {
    HMODULE h = LoadLibraryA("bassmax_dshow.ax");
    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    pfnReg reg   = (pfnReg)GetProcAddress(h, "DllRegisterServer");
    pfnReg unreg = (pfnReg)GetProcAddress(h, "DllUnregisterServer");

    HRESULT hr = reg();
    printf("DllRegisterServer hr=0x%08X\n", (unsigned)hr);

    WCHAR clsid[64]; StringFromGUID2(CLSID_BassMaxT, clsid, 64);
    WCHAR key[256]; wsprintfW(key, L"CLSID\\%s", clsid);
    HKEY hk;
    LONG r = RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &hk);
    if (r == ERROR_SUCCESS) {
        WCHAR name[128]; DWORD sz = sizeof(name);
        RegQueryValueExW(hk, nullptr, 0, nullptr, (BYTE*)name, &sz);
        wprintf(L"Registered: HKCR\\%ls  name=%ls\n", key, name);
        RegCloseKey(hk);
        // 检查 InprocServer32
        WCHAR k2[300]; wsprintfW(k2, L"%s\\InprocServer32", key);
        HKEY hk2;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, k2, 0, KEY_READ, &hk2) == ERROR_SUCCESS) {
            WCHAR path[512]; DWORD s2 = sizeof(path);
            RegQueryValueExW(hk2, nullptr, 0, nullptr, (BYTE*)path, &s2);
            wprintf(L"  InprocServer32 = %ls\n", path);
            RegCloseKey(hk2);
        }
    } else {
        printf("KEY NOT FOUND after register (err %ld)\n", r);
    }

    hr = unreg();
    printf("DllUnregisterServer hr=0x%08X\n", (unsigned)hr);
    r = RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &hk);
    printf("Key exists after unregister: %s\n", (r == ERROR_SUCCESS) ? "YES (bad)" : "NO (good)");
    if (r == ERROR_SUCCESS) RegCloseKey(hk);

    FreeLibrary(h);
    return 0;
}
