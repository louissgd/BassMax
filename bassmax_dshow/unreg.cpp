// unreg.cpp — 调用 DllUnregisterServer 并清理残留注册表项
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: unreg <ax path>\n"); return 2; }
    CoInitialize(nullptr);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary FAILED err=%lu\n", GetLastError()); return 1; }
    typedef HRESULT (WINAPI *PFN)();
    PFN unreg = (PFN)GetProcAddress(h, "DllUnregisterServer");
    if (!unreg) { printf("no DllUnregisterServer export\n"); return 1; }
    HRESULT hr = unreg();
    printf("DllUnregisterServer hr=0x%08lX\n", (unsigned long)hr);
    FreeLibrary(h);
    return 0;
}
