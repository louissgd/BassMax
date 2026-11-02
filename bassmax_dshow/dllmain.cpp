// dllmain.cpp  —  BassMax DirectShow 滤镜的 COM 入口、类厂与自注册
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

// ---------- 类厂 (同一个 DLL 导出滤镜与属性页两个 CLSID) ----------
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
        else if (IsEqualGUID(m_clsid, CLSID_BassMaxPage)) p = CBassMaxPage::CreateInstance(pUnk, &hr);
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
static const REGFILTERPINS g_pins[] = {
    { (LPWSTR)L"In",  FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, 2, g_mtTypes },
    { (LPWSTR)L"Out", FALSE, TRUE,  FALSE, FALSE, &CLSID_NULL, nullptr, 2, g_mtTypes },
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

STDAPI DllRegisterServer() {
    HRESULT hrCo  = CoInitialize(nullptr);      // regsvr32 自身未初始化 COM
    HRESULT hrRet = S_OK;

    WCHAR path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(g_hModule, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) hrRet = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);

    WCHAR clsid[64]; CLSIDToStr(CLSID_BassMax, clsid);
    WCHAR pclsid[64]; CLSIDToStr(CLSID_BassMaxPage, pclsid);
    WCHAR key[320];

    // 1) 滤镜 CLSID 名称 (播放器/GraphEdit 显示名)
    if (SUCCEEDED(hrRet)) {
        wsprintfW(key, L"CLSID\\%s", clsid);
        hrRet = CreateKeyStr(key, nullptr, L"BassMax Audio Enhancer");
    }
    // 2) 滤镜 InprocServer32 —— 指向本 .ax 的实际路径
    if (SUCCEEDED(hrRet)) {
        wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid);
        hrRet = CreateKeyStr(key, nullptr, path);
        if (SUCCEEDED(hrRet)) hrRet = CreateKeyStr(key, L"ThreadingModel", L"Both");
    }
    // 3) 属性页 CLSID (播放器按此创建"属性"页)
    if (SUCCEEDED(hrRet)) {
        wsprintfW(key, L"CLSID\\%s", pclsid);
        hrRet = CreateKeyStr(key, nullptr, L"BassMax 音频增强 设置");
    }
    if (SUCCEEDED(hrRet)) {
        wsprintfW(key, L"CLSID\\%s\\InprocServer32", pclsid);
        hrRet = CreateKeyStr(key, nullptr, path);
        if (SUCCEEDED(hrRet)) hrRet = CreateKeyStr(key, L"ThreadingModel", L"Both");
    }
    // 3) 关键: 用 IFilterMapper2 写 Instance / FilterData。
    //    没有这一步, 滤镜虽然存在于注册表, 但 IFilterMapper2 枚举不到,
    //    MPC-HC / PotPlayer 的"添加滤镜"列表里就永远看不到它。
    if (SUCCEEDED(hrRet)) {
        IFilterMapper2* pF = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFilterMapper2, (void**)&pF);
        if (FAILED(hr)) {
            hrRet = hr;
        } else {
            REGFILTER2 rf2 = {};
            rf2.dwVersion = 1;
            rf2.dwMerit   = g_setup.dwMerit;
            rf2.cPins     = g_setup.nPins;
            rf2.rgPins    = g_setup.lpPin;

            // 主类别: DirectShow Filters (GraphEdit / MPC-HC 外部滤镜列表)
            hrRet = pF->RegisterFilter(CLSID_BassMax, g_setup.strName, nullptr,
                                       &CAT_Legacy, nullptr, &rf2);
            // 附加类别: Audio Effects (部分播放器按此分类列出音频滤镜)
            pF->RegisterFilter(CLSID_BassMax, g_setup.strName, nullptr,
                               &CAT_AudioEffects, nullptr, &rf2);
            pF->Release();
        }
    }

    if (SUCCEEDED(hrCo)) CoUninitialize();
    return hrRet;
}

STDAPI DllUnregisterServer() {
    HRESULT hrCo = CoInitialize(nullptr);
    WCHAR clsid[64];  CLSIDToStr(CLSID_BassMax, clsid);
    WCHAR pclsid[64]; CLSIDToStr(CLSID_BassMaxPage, pclsid);
    WCHAR key[320];

    IFilterMapper2* pF = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFilterMapper2, (void**)&pF))) {
        pF->UnregisterFilter(&CAT_Legacy, nullptr, CLSID_BassMax);
        pF->UnregisterFilter(&CAT_AudioEffects, nullptr, CLSID_BassMax);
        pF->Release();
    }
    // 连同 Instance / Category / InprocServer32 一并清除
    wsprintfW(key, L"CLSID\\%s", clsid);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key);
    wsprintfW(key, L"CLSID\\%s", pclsid);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key);

    if (SUCCEEDED(hrCo)) CoUninitialize();
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!IsEqualGUID(rclsid, CLSID_BassMax) && !IsEqualGUID(rclsid, CLSID_BassMaxPage))
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
