// 验证: 注册 → 用 IFilterMapper2 枚举 → 播放器列表里能不能看到
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: test_reg2 <ax path>\n"); return 2; }
    CoInitialize(nullptr);

    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary FAILED err=%lu\n", GetLastError()); return 1; }
    printf("[1] LoadLibrary OK\n");

    typedef HRESULT (WINAPI *PFN)();
    PFN reg = (PFN)GetProcAddress(h, "DllRegisterServer");
    if (!reg) { printf("no DllRegisterServer export\n"); return 1; }
    HRESULT hr = reg();
    printf("[2] DllRegisterServer hr=0x%08lX %s\n", (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAILED");

    IFilterMapper2* pF = nullptr;
    hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IFilterMapper2, (void**)&pF);
    if (FAILED(hr)) { printf("FilterMapper2 FAILED 0x%08lX\n", (unsigned long)hr); return 1; }

    IEnumMoniker* pE = nullptr;
    GUID inTypes[2] = { MEDIATYPE_Audio, MEDIASUBTYPE_PCM };
    hr = pF->EnumMatchingFilters(&pE, 0, FALSE, MERIT_DO_NOT_USE + 1,
                                 TRUE, 1, inTypes, nullptr, nullptr,
                                 FALSE, FALSE, 0, nullptr, nullptr, nullptr);
    printf("[3] EnumMatchingFilters(merit>DO_NOT_USE) hr=0x%08lX %s\n", (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAILED");

    int total = 0, found = 0;
    if (SUCCEEDED(hr) && pE) {
        IMoniker* pM = nullptr; ULONG got = 0;
        while (pE->Next(1, &pM, &got) == S_OK && got == 1) {
            LPOLESTR name = nullptr;
            if (SUCCEEDED(pM->GetDisplayName(nullptr, nullptr, &name)) && name) {
                ++total;
                if (wcsstr(name, L"C41F7D22") || wcsstr(name, L"BassMax")) { ++found; wprintf(L"     >>> FOUND: %s\n", name); }
                else if (total <= 5) wprintf(L"     sample: %s\n", name);
                CoTaskMemFree(name);
            }
            pM->Release();
        }
        pE->Release();
    }
    printf("[4] 音频类滤镜枚举总数=%d, 其中 BassMax=%d\n", total, found);
    pF->Release();

    // [5] 按注册表信息真实创建滤镜 (播放器就是这么做的)
    CLSID clsid = { 0xc41f7d22, 0x6b18, 0x4a93, { 0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1 } };
    IBaseFilter* pBF = nullptr;
    HRESULT hr2 = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&pBF);
    if (SUCCEEDED(hr2) && pBF) {
        printf("[5] CoCreateInstance(IBaseFilter) OK\n");
        IPin* pIn = nullptr; IPin* pOut = nullptr;
        if (SUCCEEDED(pBF->FindPin(L"In", &pIn)) && pIn) printf("    输入引脚 OK\n");
        else printf("    输入引脚 未找到!\n");
        if (SUCCEEDED(pBF->FindPin(L"Out", &pOut)) && pOut) printf("    输出引脚 OK\n");
        else printf("    输出引脚 未找到!\n");
        if (pIn) pIn->Release();
        if (pOut) pOut->Release();
        pBF->Release();
    } else {
        printf("[5] CoCreateInstance FAILED 0x%08lX (InprocServer32 路径不对?)\n", (unsigned long)hr2);
    }

    CoUninitialize();
    return found > 0 ? 0 : 3;
}
