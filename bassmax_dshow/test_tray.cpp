// 验证托盘图标窗口的创建与销毁
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

static const GUID CLSID_F = { 0xc41f7d22, 0x6b18, 0x4a93, { 0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1 } };

static HWND FindTray() { return FindWindowExW(HWND_MESSAGE, nullptr, L"BassMaxTrayWnd", nullptr); }

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: test_tray <ax>\n"); return 2; }
    CoInitialize(nullptr);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary FAILED\n"); return 1; }
    typedef HRESULT (WINAPI *PFN)();
    ((PFN)GetProcAddress(h, "DllRegisterServer"))();

    printf("[0] 创建前托盘窗口: %s\n", FindTray() ? "存在" : "无");

    IBaseFilter* pF = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_F, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&pF);
    printf("[1] 创建滤镜 hr=0x%08lX\n", (unsigned long)hr);
    Sleep(400);
    HWND tw = FindTray();
    printf("[2] 创建后托盘窗口: %s\n", tw ? "已创建 OK" : "未创建 FAIL");
    if (tw) {
        wchar_t cls[64] = {};
        GetClassNameW(tw, cls, 64);
        wprintf(L"     窗口类: %s\n", cls);
    }

    pF->Release();
    Sleep(500);
    printf("[3] 释放后托盘窗口: %s\n", FindTray() ? "仍存在 (异常, 可能残留图标)" : "已销毁 OK");

    CoUninitialize();
    return (tw && !FindTray()) ? 0 : 3;
}
