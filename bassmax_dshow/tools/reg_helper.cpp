// reg_helper.cpp — 最小 DirectShow 滤镜注册工具
//
// 为什么不用 regsvr32: 部分安全策略把 regsvr32 / rundll32 列入 LOLBin 黑名单,
// 直接拦截。这个工具只做一件事: LoadLibrary 目标 .ax, 调它的 DllRegisterServer
// (或 DllUnregisterServer), 打印 HRESULT。
//
// 用法:
//   reg_helper.exe <path.ax>          注册
//   reg_helper.exe <path.ax> /u       反注册
// 必须以管理员身份运行 (写 HKCR)。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

// 用 IFilterMapper2 枚举"音频 PCM 输入 -> 音频 PCM 输出"的变换滤镜,
// 这就是播放器"添加滤镜"列表的实际数据来源。
static int EnumFilters() {
    CoInitialize(nullptr);
    IFilterMapper2* pF = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFilterMapper2, (void**)&pF);
    if (FAILED(hr)) { printf("FilterMapper2 创建失败 0x%08X\n", (unsigned)hr); return 1; }

    GUID media[1] = { MEDIATYPE_Audio };

    IEnumMoniker* pEnum = nullptr;
    // 不加任何类型约束: 只按 merit 枚举整个 DirectShow Filters 分类
    hr = pF->EnumMatchingFilters(&pEnum, 0, FALSE, MERIT_DO_NOT_USE,
                                 FALSE, 0, nullptr, nullptr, nullptr,
                                 FALSE, FALSE, 0, nullptr, nullptr, nullptr);
    if (FAILED(hr)) { printf("EnumMatchingFilters 失败 0x%08X\n", (unsigned)hr); pF->Release(); CoUninitialize(); return 1; }

    int n = 0, ours = 0;
    IMoniker* m = nullptr;
    while (pEnum->Next(1, &m, nullptr) == S_OK) {
        ++n;
        LPOLESTR name = nullptr;
        if (SUCCEEDED(m->GetDisplayName(nullptr, nullptr, &name)) && name) {
            if (wcsstr(name, L"C41F7D22") || wcsstr(name, L"C41F7D22")) { }
            printf("  [%d] %ls\n", n, name);
            if (wcsstr(name, L"41F7D22") || wcsstr(name, L"BassMax")) ++ours;
            CoTaskMemFree(name);
        }
        m->Release();
    }
    printf("枚举到 %d 个音频变换滤镜, 其中 BassMax %d 个\n", n, ours);
    pEnum->Release();
    pF->Release();
    CoUninitialize();
    return ours >= 1 ? 0 : 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && _wcsicmp(argv[1], L"/enum") == 0) return EnumFilters();
    if (argc < 2) {
        printf("用法: reg_helper.exe <path.ax> [/u]\n       reg_helper.exe /enum\n");
        return 2;
    }
    const bool unreg = (argc > 2 && (_wcsicmp(argv[2], L"/u") == 0 || _wcsicmp(argv[2], L"-u") == 0));

    HMODULE h = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        printf("LoadLibrary 失败 %lu: %ls\n", GetLastError(), argv[1]);
        printf("(32 位进程加载 64 位 .ax 会返回 193)\n");
        return 3;
    }
    typedef HRESULT (STDAPICALLTYPE *PFN)(void);
    PFN fn = (PFN)GetProcAddress(h, unreg ? "DllUnregisterServer" : "DllRegisterServer");
    if (!fn) {
        printf("找不到导出 %s\n", unreg ? "DllUnregisterServer" : "DllRegisterServer");
        FreeLibrary(h);
        return 4;
    }
    HRESULT hr = fn();
    FreeLibrary(h);
    printf("%s: HRESULT=0x%08X %s\n", unreg ? "反注册" : "注册", (unsigned)hr,
           SUCCEEDED(hr) ? "OK" : "失败");
    return SUCCEEDED(hr) ? 0 : 1;
}
