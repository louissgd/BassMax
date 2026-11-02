// test_com.cpp — 冒烟测试: 加载 bassmax_dshow.ax, 取类厂, 创建滤镜实例 (IBaseFilter)
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

typedef HRESULT (STDAPICALLTYPE *pfnGCO)(REFCLSID, REFIID, void**);
static const GUID CLSID_BassMaxT = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1}
};

int main() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HMODULE h = LoadLibraryA("bassmax_dshow.ax");
    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    pfnGCO gco = (pfnGCO)GetProcAddress(h, "DllGetClassObject");
    if (!gco) { printf("no DllGetClassObject export\n"); return 2; }

    IClassFactory* fac = nullptr;
    HRESULT hr = gco(CLSID_BassMaxT, IID_IClassFactory, (void**)&fac);
    printf("DllGetClassObject hr=0x%08X fac=%p\n", (unsigned)hr, (void*)fac);
    if (FAILED(hr) || !fac) return 3;

    IBaseFilter* f = nullptr;
    hr = fac->CreateInstance(nullptr, IID_IBaseFilter, (void**)&f);
    printf("CreateInstance(IBaseFilter) hr=0x%08X filter=%p\n", (unsigned)hr, (void*)f);
    if (FAILED(hr) || !f) return 4;

    FILTER_INFO fi; ZeroMemory(&fi, sizeof(fi));
    if (SUCCEEDED(f->QueryFilterInfo(&fi))) {
        wprintf(L"Filter name = %ls\n", fi.achName);
        if (fi.pGraph) fi.pGraph->Release();
    }
    f->Release();
    fac->Release();
    FreeLibrary(h);
    CoUninitialize();
    printf("SMOKE TEST OK\n");
    return 0;
}
