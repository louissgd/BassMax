// dllmain.cpp  —  BassMax DirectShow 滤镜的 COM 入口、类厂与自注册
//
// 1.0.6: 回归单管线 —— 一个 .ax 只注册一个滤镜 + 一个属性页
//   CLSID_BassMax      CTransInPlaceFilter  "BassMax Audio Enhancer"
//   CLSID_BassMaxPage  属性页 (参数统一走 HKCU\Software\BassMax)
//
// 早期双管线尝试曾额外注册 CLSID_BassMaxXform (CTransformFilter, "…(MPC-BE)"),
// 想让 MPC-BE 把外部 PCM transform 插进音频链。查 MPC-BE 官方源码后确认:
// 内置 Audio Switcher 以 MERIT64_HIGHEST 无条件占位, 外部 PCM→PCM 滤镜
// 根本进不了链路 (被创建后立刻销毁)。MPC-BE 现由独立的 bassmax_decoder.ax
// (C9B4, 解码器角色) 接管, 见 README「六·补」。所以 C9B3 已停用:
//   * 不再实例化、不再注册;
//   * 注册/卸载时主动清除旧版留下的 C9B3 注册表残留, 免得"添加滤镜"列表里
//     一直挂着一个 CoCreate 不到的幽灵条目。
#include <windows.h>
#include <objbase.h>
#include "bassmax_filter.h"
#include "bassmax_page.h"

// DirectShow 滤镜类别 (用自有名称, 避免与 strmiids.lib 中的同名符号冲突)
static const GUID CAT_Legacy = {
    0x083863F1, 0x70DE, 0x11d0, {0xBD,0x40,0x00,0xA0,0xC9,0x11,0xCE,0x86}
};
static const GUID CAT_AudioEffects = {
    0x4a2286e0, 0x7bef, 0x11ce, {0x9b,0xd9,0x00,0x00,0xe2,0x02,0x59,0x9c}
};

static HMODULE g_hModule = nullptr;

// ---- baseclasses 需要的全局变量 (因排除 dllentry.cpp 而自行提供) ----
// 说明: 我们不使用 AMovieDllRegisterServer 的模板注册机制, g_Templates 仅需存在以满足链接;
//       g_hInst / g_amPlatform / g_osInfo 供 baseclasses 内部资源加载与平台判断使用。
HINSTANCE     g_hInst      = nullptr;
DWORD         g_amPlatform = VER_PLATFORM_WIN32_NT;
OSVERSIONINFO g_osInfo     = {};
CFactoryTemplate g_Templates[1] = {};   // 空模板数组
int           g_cTemplates = 0;

// ---------- 类厂 (同一个 DLL 导出滤镜 + 属性页两个 CLSID) ----------
class CFactory : public IClassFactory {
    LONG   m_cRef  = 1;
    CLSID  m_clsid;
public:
    explicit CFactory(const CLSID& c) : m_clsid(c) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) {
        if (iid == IID_IUnknown || iid == IID_IClassFactory) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() { LONG r = InterlockedDecrement(&m_cRef); if (!r) delete this; return r; }
    STDMETHODIMP CreateInstance(LPUNKNOWN pUnk, REFIID iid, void** pp) {
        if (pUnk) return CLASS_E_NOAGGREGATION;
        *pp = nullptr;
        HRESULT hr = S_OK;
        CUnknown* p = nullptr;
        if (IsEqualGUID(m_clsid, CLSID_BassMax))          p = BassMaxFilter::CreateInstance(pUnk, &hr);
        else if (IsEqualGUID(m_clsid, CLSID_BassMaxPage))  p = CBassMaxPage::CreateInstance(pUnk, &hr);
        else return CLASS_E_CLASSNOTAVAILABLE;
        if (!p) return hr;
        // CUnknown 初始引用计数为 0: 先 AddRef 到 1, QI 再 +1 (由 *pp 持有),
        // 之后 Release 回到 1。这与 baseclasses 官方类厂一致。
        p->NonDelegatingAddRef();
        hr = p->NonDelegatingQueryInterface(iid, pp);
        p->NonDelegatingRelease();   // QI 失败时回到 0 自动析构; 成功时 *pp 持有 1 个引用
        return hr;
    }
    STDMETHODIMP LockServer(BOOL) { return S_OK; }
};

// ---------- 滤镜安装数据 (供 IFilterMapper2 生成 Instance/FilterData) ----------
// 引脚媒体类型: PCM 与 IEEE float (WAVEFORMATEXTENSIBLE 也映射到这两个子类型)
static const REGPINTYPES g_mtTypes[] = {
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_PCM },
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_IEEE_FLOAT },
};
// baseclasses 里 AMOVIESETUP_PIN 就是 REGFILTERPINS 的 typedef, 两边通用
static const REGFILTERPINS g_pins[] = {
    { (LPWSTR)L"In",  FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, 2, (REGPINTYPES*)g_mtTypes },
    { (LPWSTR)L"Out", FALSE, TRUE,  FALSE, FALSE, &CLSID_NULL, nullptr, 2, (REGPINTYPES*)g_mtTypes },
};
// merit 用 MERIT_UNLIKELY: 高于 MERIT_DO_NOT_USE, 这样 MPC-HC / PotPlayer 的
// "添加滤镜" 列表 (部分实现以 MERIT_DO_NOT_USE+1 为下限) 能枚举到;
// 又足够低, 播放器自动构链时不会把它插进去 (用户需手动添加为外部滤镜)。
static const AMOVIESETUP_FILTER g_setup = {
    &CLSID_BassMax, L"BassMax Audio Enhancer", MERIT_UNLIKELY, 2, g_pins
};

// ---------- 注册辅助 ----------
static HRESULT SetStr(HKEY hk, LPCWSTR name, LPCWSTR val) {
    LONG r = RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
    return (r == ERROR_SUCCESS) ? S_OK : HRESULT_FROM_WIN32((DWORD)r);
}
static HRESULT CreateKeyStr(LPCWSTR path, LPCWSTR name, LPCWSTR val) {
    HKEY hk; DWORD disp;
    LONG r = RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, &disp);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32((DWORD)r);
    HRESULT hr = SetStr(hk, name, val);
    RegCloseKey(hk);
    return hr;
}

// 将 CLSID 字符串化
static void CLSIDToStr(const GUID& g, WCHAR* buf) {
    StringFromGUID2(g, buf, 40);
}

// 写 CLSID 名称 + InprocServer32 (滤镜与属性页都走这套)
static HRESULT RegisterComServer(const GUID& clsid, LPCWSTR name, LPCWSTR path) {
    WCHAR s[64]; CLSIDToStr(clsid, s);
    WCHAR key[320];
    wsprintfW(key, L"CLSID\\%s", s);
    HRESULT hr = CreateKeyStr(key, nullptr, name);
    if (FAILED(hr)) return hr;
    wsprintfW(key, L"CLSID\\%s\\InprocServer32", s);
    hr = CreateKeyStr(key, nullptr, path);
    if (SUCCEEDED(hr)) hr = CreateKeyStr(key, L"ThreadingModel", L"Both");
    return hr;
}

// 用 IFilterMapper2 写 Instance / FilterData。
// 没有这一步, 滤镜虽然存在于注册表, 但 IFilterMapper2 枚举不到,
// 播放器的"添加滤镜"列表里就永远看不到它。
static HRESULT RegisterWithMapper(IFilterMapper2* pF, const AMOVIESETUP_FILTER& setup) {
    REGFILTER2 rf2 = {};
    rf2.dwVersion  = 1;
    rf2.dwMerit    = setup.dwMerit;
    rf2.cPins      = setup.nPins;
    rf2.rgPins     = setup.lpPin;

    // 主类别: DirectShow Filters (GraphEdit / MPC-HC 外部滤镜列表)
    HRESULT hr = pF->RegisterFilter(*setup.clsID, setup.strName, nullptr, &CAT_Legacy, nullptr, &rf2);
    // 附加类别: Audio Effects (部分播放器按此分类列出音频滤镜)
    pF->RegisterFilter(*setup.clsID, setup.strName, nullptr, &CAT_AudioEffects, nullptr, &rf2);
    return hr;
}

// 清理早期双管线尝试遗留的 C9B3 注册 (已停用的 CTransform 版)。
// 两个类别的 Instance/FilterData 和 CLSID 键都要删干净, 否则播放器列表里会
// 一直挂着一个 CoCreate 不到的幽灵条目, 用户很容易选错。注册与卸载都会调用,
// 因此老机器升级时跑一次 install.bat 就能自愈。
static void PurgeLegacyClsid(IFilterMapper2* pF, const GUID& clsid) {
    if (pF) {
        pF->UnregisterFilter(&CAT_Legacy, nullptr, clsid);
        pF->UnregisterFilter(&CAT_AudioEffects, nullptr, clsid);
    }
    WCHAR s[64]; CLSIDToStr(clsid, s);
    WCHAR key[320]; wsprintfW(key, L"CLSID\\%s", s);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key);
}

STDAPI DllRegisterServer() {
    HRESULT hrCo  = CoInitialize(nullptr);      // regsvr32 自身未初始化 COM
    HRESULT hrRet = S_OK;

    WCHAR path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(g_hModule, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) hrRet = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);

    // 1) 滤镜 + 属性页的 CLSID 名称 / InprocServer32
    if (SUCCEEDED(hrRet)) hrRet = RegisterComServer(CLSID_BassMax,     L"BassMax Audio Enhancer", path);
    if (SUCCEEDED(hrRet)) hrRet = RegisterComServer(CLSID_BassMaxPage, L"BassMax 音频增强 设置",  path);

    // 2) 映射器注册 (Instance / FilterData) + 清理旧版 C9B3 残留
    if (SUCCEEDED(hrRet)) {
        IFilterMapper2* pF = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFilterMapper2, (void**)&pF);
        if (FAILED(hr)) {
            hrRet = hr;
            PurgeLegacyClsid(nullptr, CLSID_BassMaxXform);
        } else {
            hrRet = RegisterWithMapper(pF, g_setup);
            PurgeLegacyClsid(pF, CLSID_BassMaxXform);
            pF->Release();
        }
    }

    if (SUCCEEDED(hrCo)) CoUninitialize();
    return hrRet;
}

STDAPI DllUnregisterServer() {
    HRESULT hrCo = CoInitialize(nullptr);

    IFilterMapper2* pF = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFilterMapper2, (void**)&pF))) {
        pF->UnregisterFilter(&CAT_Legacy, nullptr, CLSID_BassMax);
        pF->UnregisterFilter(&CAT_AudioEffects, nullptr, CLSID_BassMax);
    }
    PurgeLegacyClsid(pF, CLSID_BassMaxXform);
    if (pF) pF->Release();

    // 连同 Instance / Category / InprocServer32 一并清除
    const GUID* all[] = { &CLSID_BassMax, &CLSID_BassMaxPage };
    for (int i = 0; i < 2; ++i) {
        WCHAR s[64]; CLSIDToStr(*all[i], s);
        WCHAR key[320]; wsprintfW(key, L"CLSID\\%s", s);
        RegDeleteTreeW(HKEY_CLASSES_ROOT, key);
    }

    if (SUCCEEDED(hrCo)) CoUninitialize();
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!IsEqualGUID(rclsid, CLSID_BassMax) &&
        !IsEqualGUID(rclsid, CLSID_BassMaxPage))
        return CLASS_E_CLASSNOTAVAILABLE;
    if (riid != IID_IClassFactory && riid != IID_IUnknown) return E_NOINTERFACE;
    CFactory* f = new CFactory(rclsid);
    if (!f) return E_OUTOFMEMORY;
    *ppv = static_cast<IClassFactory*>(f);
    return S_OK;
}

STDAPI DllCanUnloadNow() {
    // 滤镜为进程内长驻; 安全起见始终返回 S_FALSE (不卸载)
    return S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason, LPVOID) {
    if (ul_reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        g_hInst   = hModule;
        g_osInfo.dwOSVersionInfoSize = sizeof(g_osInfo);
        if (GetVersionEx(&g_osInfo)) g_amPlatform = g_osInfo.dwPlatformId;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
