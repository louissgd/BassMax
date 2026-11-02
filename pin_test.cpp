// pin_test.cpp — 直接加载 bassmax_dshow.ax, 用 IPin::QueryAccept 实测各采样率是否被接受
// 这条路径与播放器协商时走的是同一个函数 (CheckInputType -> ParseFmt), 属于活体验证,
// 不是"看代码猜"。
#include <windows.h>
#include <mmreg.h>
#include <dshow.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

// 手工声明, 避免依赖 baseclasses 的 uuids.h
static const GUID kMediaTypeAudio =
    { 0x73647561, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } }; // MEDIATYPE_Audio
static const GUID kSubFloat =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } }; // IEEE_FLOAT
static const GUID kSubPcm =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } }; // PCM
static const GUID kFmtWaveEx =
    { 0x05589f81, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } }; // FORMAT_WaveFormatEx
static const GUID kClsidBassMax =
    { 0xc41f7d22, 0x6b18, 0x4a93, { 0x8e, 0x52, 0x1d, 0x7a, 0x4f, 0x60, 0xc9, 0xb1 } };

typedef HRESULT(STDAPICALLTYPE* PFN_GetClassObject)(REFCLSID, REFIID, LPVOID*);

static AM_MEDIA_TYPE MakeMt(WAVEFORMATEXTENSIBLE* wfe, bool isFloat, DWORD rate) {
    ZeroMemory(wfe, sizeof(*wfe));
    wfe->Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wfe->Format.nChannels       = 2;
    wfe->Format.nSamplesPerSec  = rate;
    wfe->Format.wBitsPerSample  = isFloat ? 32 : 16;
    wfe->Format.nBlockAlign     = (WORD)(wfe->Format.nChannels * wfe->Format.wBitsPerSample / 8);
    wfe->Format.nAvgBytesPerSec = rate * wfe->Format.nBlockAlign;
    wfe->Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfe->Samples.wValidBitsPerSample = wfe->Format.wBitsPerSample;
    wfe->dwChannelMask          = 3;
    wfe->SubFormat              = isFloat ? kSubFloat : kSubPcm;

    AM_MEDIA_TYPE mt;
    ZeroMemory(&mt, sizeof(mt));
    mt.majortype  = kMediaTypeAudio;
    mt.subtype    = isFloat ? kSubFloat : kSubPcm;
    mt.bFixedSizeSamples = TRUE;
    mt.lSampleSize = wfe->Format.nBlockAlign;
    mt.formattype = kFmtWaveEx;
    mt.cbFormat   = sizeof(WAVEFORMATEXTENSIBLE);
    mt.pbFormat   = (BYTE*)wfe;
    return mt;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: pin_test.exe <path to bassmax_dshow.ax>\n"); return 2; }
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printf("DLL: %s\n\n", argv[1]);

    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary 失败: %lu\n", GetLastError()); return 1; }
    PFN_GetClassObject fn = (PFN_GetClassObject)GetProcAddress(h, "DllGetClassObject");
    if (!fn) { printf("找不到 DllGetClassObject\n"); return 1; }

    IClassFactory* cf = nullptr;
    HRESULT hr = fn(kClsidBassMax, __uuidof(IClassFactory), (void**)&cf);
    if (FAILED(hr)) { printf("DllGetClassObject 失败: 0x%08lX\n", hr); return 1; }

    IBaseFilter* bf = nullptr;
    hr = cf->CreateInstance(nullptr, __uuidof(IBaseFilter), (void**)&bf);
    if (FAILED(hr)) { printf("CreateInstance 失败: 0x%08lX\n", hr); return 1; }

    IEnumPins* ep = nullptr;
    bf->EnumPins(&ep);
    IPin* pin = nullptr;
    IPin* found = nullptr;
    ULONG got = 0;
    while (ep && ep->Next(1, &pin, &got) == S_OK && got == 1) {
        PIN_DIRECTION d;
        if (SUCCEEDED(pin->QueryDirection(&d)) && d == PINDIR_INPUT) { found = pin; break; }
        pin->Release();
        pin = nullptr;
    }
    if (!found) { printf("找不到输入引脚\n"); return 1; }

    PIN_INFO pi;
    if (SUCCEEDED(found->QueryPinInfo(&pi))) {
        printf("输入引脚: %ls\n\n", pi.achName);
        if (pi.pFilter) pi.pFilter->Release();
    }

    struct Case { DWORD rate; bool isFloat; const char* note; };
    const Case cases[] = {
        {  44100,  true,  "音乐 44.1k 基准" },
        {  96000,  true,  "" },
        { 192000,  true,  "" },
        { 352800,  true,  "DSD64 解码" },
        { 705600,  true,  "DSD128 解码" },
        {1411200,  true,  "DSD256 解码 <- 本次目标" },
        {1536000,  true,  "新上限值" },
        {2822400,  true,  "DSD512 (应拒绝)" },
        {1411200,  false, "DSD256 但有源是 16bit PCM" },
    };

    printf("%-10s %-10s %-8s %s\n", "采样率", "格式", "结果", "说明");
    for (const Case& c : cases) {
        WAVEFORMATEXTENSIBLE wfe;
        AM_MEDIA_TYPE mt = MakeMt(&wfe, c.isFloat, c.rate);
        HRESULT r = found->QueryAccept(&mt);
        const char* verdict = (r == S_OK) ? "接受" : (r == S_FALSE ? "拒绝(S_FALSE)" : "拒绝");
        printf("%-10lu %-10s %-8s %s  (0x%08lX)\n",
               (unsigned long)c.rate, c.isFloat ? "float32" : "pcm16", verdict, c.note, (unsigned long)r);
    }

    found->Release();
    if (ep) ep->Release();
    bf->Release();
    cf->Release();
    FreeLibrary(h);
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return 0;
}
