// 验证: 注册 -> ISpecifyPropertyPages -> 创建属性页 -> 真正加载对话框并检查控件
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

static const GUID CLSID_F = { 0xc41f7d22, 0x6b18, 0x4a93, { 0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1 } };
static const GUID CLSID_P = { 0xc41f7d22, 0x6b18, 0x4a93, { 0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb2 } };

static int g_btnTrack = 0, g_static = 0;

static BOOL CALLBACK ChildProc(HWND h, LPARAM) {
    wchar_t cls[64] = {}, txt[128] = {};
    GetClassNameW(h, cls, 64);
    GetWindowTextW(h, txt, 128);
    int id = GetDlgCtrlID(h);
    wprintf(L"      [子窗口] id=%d class=%-22s text=%s\n", id, cls, txt);
    if (_wcsicmp(cls, L"msctls_trackbar32") == 0) ++g_btnTrack;
    if (_wcsicmp(cls, L"Static") == 0) ++g_static;
    return TRUE;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: test_props <ax path>\n"); return 2; }
    CoInitialize(nullptr);

    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary FAILED %lu\n", GetLastError()); return 1; }
    typedef HRESULT (WINAPI *PFN)();
    HRESULT hr = ((PFN)GetProcAddress(h, "DllRegisterServer"))();
    printf("[1] 注册 hr=0x%08lX\n", (unsigned long)hr);

    IBaseFilter* pF = nullptr;
    hr = CoCreateInstance(CLSID_F, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&pF);
    printf("[2] 滤镜 CoCreate hr=0x%08lX %s\n", (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");
    if (FAILED(hr)) return 1;

    ISpecifyPropertyPages* pSPP = nullptr;
    hr = pF->QueryInterface(IID_ISpecifyPropertyPages, (void**)&pSPP);
    printf("[3] QI ISpecifyPropertyPages hr=0x%08lX %s\n", (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");

    if (SUCCEEDED(hr)) {
        CAUUID u = {};
        hr = pSPP->GetPages(&u);
        printf("[4] GetPages hr=0x%08lX cElems=%lu\n", (unsigned long)hr, u.cElems);
        if (u.cElems) {
            wchar_t g[64] = {};
            StringFromGUID2(u.pElems[0], g, 64);
            wprintf(L"    页面 CLSID = %s  与预期一致: %s\n", g, IsEqualGUID(u.pElems[0], CLSID_P) ? L"是" : L"否");
            CoTaskMemFree(u.pElems);
        }
        pSPP->Release();
    }

    IPropertyPage* pPage = nullptr;
    hr = CoCreateInstance(CLSID_P, nullptr, CLSCTX_INPROC_SERVER, IID_IPropertyPage, (void**)&pPage);
    printf("[5] 属性页 CoCreate hr=0x%08lX %s\n", (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");
    if (FAILED(hr)) { pF->Release(); return 1; }

    HWND parent = CreateWindowExW(0, L"STATIC", L"parent", WS_OVERLAPPEDWINDOW,
                                  0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr);
    IUnknown* objs[1] = { pF };
    hr = pPage->SetObjects(1, objs);
    printf("[6] SetObjects hr=0x%08lX\n", (unsigned long)hr);

    RECT rc = { 0, 0, 220, 100 };
    hr = pPage->Activate(parent, &rc, FALSE);
    printf("[7] Activate hr=0x%08lX %s\n", (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");

    PROPPAGEINFO info = {};
    info.cb = sizeof(info);
    if (SUCCEEDED(pPage->GetPageInfo(&info))) {
        wprintf(L"[8] 页面标题 = %s\n", info.pszTitle ? info.pszTitle : L"(null)");
        if (info.pszTitle) CoTaskMemFree(info.pszTitle);
        if (info.pszDocString) CoTaskMemFree(info.pszDocString);
    }

    printf("[9] 对话框子控件:\n");
    EnumChildWindows(parent, ChildProc, 0);
    printf("[10] 结论: 轨迹条=%d (期望 2), 静态文本=%d (期望 >=4)\n", g_btnTrack, g_static);

    pPage->Deactivate();
    pPage->Release();
    if (parent) DestroyWindow(parent);
    pF->Release();
    CoUninitialize();
    return (g_btnTrack >= 2) ? 0 : 3;
}
