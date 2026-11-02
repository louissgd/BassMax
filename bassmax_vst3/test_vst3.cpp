// test_vst3.cpp — 冒烟测试: 加载 BassMax.vst3, 调用 GetPluginFactory
#include <windows.h>
#include <stdio.h>
typedef void* (*pfnGetFactory)();
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "BassMax.vst3";
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed %lu (%s)\n", GetLastError(), path); return 1; }
    pfnGetFactory g = (pfnGetFactory)GetProcAddress(h, "GetPluginFactory");
    if (!g) { printf("no GetPluginFactory export\n"); return 2; }
    void* fac = g();
    printf("GetPluginFactory -> factory=%p  %s\n", fac, fac ? "OK" : "NULL");
    FreeLibrary(h);
    return fac ? 0 : 3;
}
