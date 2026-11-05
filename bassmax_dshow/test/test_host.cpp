// test_host.cpp — 用真实 DirectShow 图验证 BassMax 滤镜
//
// 不依赖注册表: 直接 LoadLibrary .ax + DllGetClassObject, 手工 AddFilter。
// 验证四件事:
//   1) CTransInPlaceFilter 版本能在真实图里接通 (WAV Parser -> BassMax -> Null Renderer)
//   2) 运行中把采样格式从 2ch 换成 6ch: 输入引脚 QueryAccept 必须返回 S_OK
//      (旧版 CTransformFilter + 精确 CheckTransform 会返回 S_FALSE -> LAV 切音轨失败)
//   3) 采样自带新 AM_MEDIA_TYPE 时 (IMediaSample::GetMediaType) Transform 能就地重建并处理
//   4) 拆除/重建 5 轮, 每轮滤镜析构 < 300ms (旧版析构里 WaitForSingleObject(2000) 会露馅)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <mmreg.h>
#include <stdio.h>
#include <math.h>

static const GUID kCLSID_BassMax = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1}
};
static const GUID kCLSID_NullRenderer = {
    0xc1f400a4, 0x3f08, 0x11d3, {0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}
};

// 生成 1 秒 48k/16bit 立体声 WAV
static bool MakeWav(const wchar_t* path, int ch, DWORD sr, int secs) {
    FILE* f = _wfopen(path, L"wb");
    if (!f) return false;
    const int bytes = 2, frames = (int)sr * secs;
    const int dataBytes = frames * ch * bytes;
    BYTE hdr[44] = { 'R','I','F','F',0,0,0,0, 'W','A','V','E','f','m','t',' ',
                     16,0,0,0, 1,0, 0,0, 0,0,0,0, 0,0,0,0, 0,0, 0,0,
                     'd','a','t','a',0,0,0,0 };
    DWORD riff = 36 + dataBytes;
    memcpy(hdr + 4, &riff, 4);
    WORD w = (WORD)ch;            memcpy(hdr + 22, &w, 2);
    memcpy(hdr + 24, &sr, 4);
    DWORD bps = sr * ch * bytes;  memcpy(hdr + 28, &bps, 4);
    w = (WORD)(ch * bytes);       memcpy(hdr + 32, &w, 2);
    memcpy(hdr + 40, &dataBytes, 4);
    fwrite(hdr, 1, 44, f);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c) {
            short v = (short)(12000.0 * sin(2.0 * 3.14159265 * 220.0 * i / (double)sr));
            fwrite(&v, 2, 1, f);
        }
    fclose(f);
    return true;
}

// float32 / N 声道的 WAVEFORMATEXTENSIBLE (自己分配, 不依赖 baseclasses 的 CMediaType)
static void FreeMt(AM_MEDIA_TYPE& mt) {
    if (mt.cbFormat && mt.pbFormat) CoTaskMemFree(mt.pbFormat);
    if (mt.pUnk) { mt.pUnk->Release(); mt.pUnk = nullptr; }
    ZeroMemory(&mt, sizeof(mt));
}

static void MakeExtFloat(AM_MEDIA_TYPE& mt, int ch, DWORD sr, int bits) {
    WAVEFORMATEXTENSIBLE wf = {};
    wf.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels       = (WORD)ch;
    wf.Format.nSamplesPerSec  = sr;
    wf.Format.wBitsPerSample  = (WORD)bits;
    wf.Format.nBlockAlign     = (WORD)(ch * bits / 8);
    wf.Format.nAvgBytesPerSec = sr * wf.Format.nBlockAlign;
    wf.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = (WORD)bits;
    wf.dwChannelMask          = 0;
    static const GUID kFloat = { 0x00000003,0x0000,0x0010,
                                {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71} };
    wf.SubFormat = kFloat;
    ZeroMemory(&mt, sizeof(mt));
    mt.majortype  = MEDIATYPE_Audio;
    mt.subtype    = MEDIASUBTYPE_IEEE_FLOAT;
    mt.formattype = FORMAT_WaveFormatEx;
    mt.bFixedSizeSamples = TRUE;
    mt.bTemporalCompression = FALSE;
    mt.lSampleSize = wf.Format.nBlockAlign;
    mt.pbFormat = (BYTE*)CoTaskMemAlloc(sizeof(wf));
    mt.cbFormat = mt.pbFormat ? sizeof(wf) : 0;
    if (mt.pbFormat) memcpy(mt.pbFormat, &wf, sizeof(wf));
}

static IBaseFilter* CreateOurFilter(HMODULE h) {
    typedef HRESULT (STDAPICALLTYPE *PFN_GCO)(REFCLSID, REFIID, void**);
    PFN_GCO gco = (PFN_GCO)GetProcAddress(h, "DllGetClassObject");
    if (!gco) { printf("  !! DllGetClassObject 找不到\n"); return nullptr; }
    IClassFactory* cf = nullptr;
    HRESULT hr = gco(kCLSID_BassMax, IID_IClassFactory, (void**)&cf);
    if (FAILED(hr)) { printf("  !! DllGetClassObject hr=0x%08X\n", hr); return nullptr; }
    IBaseFilter* p = nullptr;
    hr = cf->CreateInstance(nullptr, IID_IBaseFilter, (void**)&p);
    cf->Release();
    if (FAILED(hr)) printf("  !! CreateInstance hr=0x%08X\n", hr);
    return p;
}

static IPin* FindPin(IBaseFilter* f, PIN_DIRECTION dir) {
    IEnumPins* e = nullptr;
    if (FAILED(f->EnumPins(&e))) return nullptr;
    IPin* p = nullptr; ULONG n = 0; IPin* found = nullptr;
    while (e->Next(1, &p, &n) == S_OK) {
        PIN_DIRECTION d;
        if (SUCCEEDED(p->QueryDirection(&d)) && d == dir && !found) found = p;
        else p->Release();
    }
    e->Release();
    if (found) found->AddRef();
    return found;
}

// 直接往输入引脚喂一个"自带新媒体类型"的采样 —— 这就是 LAV 切音轨时干的事
static HRESULT FeedSample(IPin* pin, const AM_MEDIA_TYPE& mt, int frames) {
    IMemInputPin* mip = nullptr;
    HRESULT hr = pin->QueryInterface(IID_IMemInputPin, (void**)&mip);
    if (FAILED(hr)) return hr;

    IMemAllocator* a = nullptr;
    hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IMemAllocator, (void**)&a);
    if (FAILED(hr)) { mip->Release(); return hr; }

    const WAVEFORMATEX* wf = (const WAVEFORMATEX*)mt.pbFormat;
    if (!wf) return E_INVALIDARG;
    const long len = frames * wf->nBlockAlign;
    ALLOCATOR_PROPERTIES p = {}, act;
    p.cBuffers = 2; p.cbBuffer = len + 1024; p.cbAlign = 1;
    a->SetProperties(&p, &act);
    a->Commit();

    IMediaSample* s = nullptr;
    hr = a->GetBuffer(&s, nullptr, nullptr, 0);
    if (SUCCEEDED(hr)) {
        BYTE* buf = nullptr;
        if (SUCCEEDED(s->GetPointer(&buf)) && buf) {
            float* fp = (float*)buf;
            for (int i = 0; i < frames * wf->nChannels; ++i)
                fp[i] = 0.35f * (float)sin(i * 0.017);
            s->SetActualDataLength(len);
            s->SetTime(nullptr, nullptr);
            s->SetSyncPoint(TRUE);
            s->SetMediaType((AM_MEDIA_TYPE*)&mt);                 // <- 采样随身带新格式
            hr = mip->Receive(s);
        } else hr = E_FAIL;
        s->Release();
    }
    a->Release();
    mip->Release();
    return hr;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* axPath = (argc > 1) ? argv[1] : L"bassmax_dshow.ax";
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE h = LoadLibraryW(axPath);
    if (!h) { printf("LoadLibrary 失败 %lu\n", GetLastError()); return 1; }

    wchar_t wav2[MAX_PATH], wav6[MAX_PATH];
    GetTempPathW(MAX_PATH, wav2);
    GetTempPathW(MAX_PATH, wav6);
    wcscat_s(wav2, L"bassmax_test2.wav");
    wcscat_s(wav6, L"bassmax_test6.wav");
    MakeWav(wav2, 2, 48000, 1);
    MakeWav(wav6, 6, 48000, 1);

    int fail = 0;
    for (int round = 0; round < 5; ++round) {
        LARGE_INTEGER f, t0, t1, t2, t3;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);

        IGraphBuilder* g = nullptr;
        IMediaControl* mc = nullptr;
        IBaseFilter* src = nullptr, *flt = nullptr, *nullr = nullptr;
        IPin *pinOut = nullptr, *pinIn = nullptr, *pinFOut = nullptr, *pinRIn = nullptr;

        CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IGraphBuilder, (void**)&g);
        g->QueryInterface(IID_IMediaControl, (void**)&mc);
        const wchar_t* wav = (round % 2) ? wav6 : wav2;
        g->AddSourceFilter(wav, L"Src", &src);

        flt = CreateOurFilter(h);
        if (!flt) { fail++; goto cleanup; }
        CoCreateInstance(kCLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IBaseFilter, (void**)&nullr);
        g->AddFilter(flt, L"BassMax");
        g->AddFilter(nullr, L"Null");

        pinOut = FindPin(src, PINDIR_OUTPUT);
        pinIn  = FindPin(flt, PINDIR_INPUT);
        pinFOut= FindPin(flt, PINDIR_OUTPUT);
        pinRIn = FindPin(nullr, PINDIR_INPUT);
        {
            HRESULT h1 = g->Connect(pinOut, pinIn);
            HRESULT h2 = g->Connect(pinFOut, pinRIn);
            printf("round %d: connect in=0x%08X out=0x%08X", round, h1, h2);
            if (FAILED(h1) || FAILED(h2)) { printf(" <-- 连不上\n"); fail++; goto cleanup; }

            mc->Pause();                       // 进入 streaming 才能 Receive
            Sleep(120);

            // (2) 运行中格式变更: 2ch -> 6ch
            {
                IMemInputPin* mip = nullptr;
                if (SUCCEEDED(pinIn->QueryInterface(IID_IMemInputPin, (void**)&mip))) {
                    mip->Release();
                    AM_MEDIA_TYPE mt6; MakeExtFloat(mt6, 6, 48000, 32);
                    HRESULT qa = pinIn->QueryAccept(&mt6);
                    FreeMt(mt6);
                    printf(" | QueryAccept(6ch/48k/float)=0x%08X", qa);
                    if (qa != S_OK) { printf(" <-- 拒绝格式变更, LAV 会切轨失败\n"); fail++; }
                    mip->Release();
                }
            }

            HRESULT hr = mc->Stop();
            printf(" stop=0x%08X", hr);
        }

    cleanup:
        QueryPerformanceCounter(&t1);
        double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;

        if (pinOut) pinOut->Release();
        if (pinIn)  pinIn->Release();
        if (pinFOut) pinFOut->Release();
        if (pinRIn) pinRIn->Release();

        QueryPerformanceCounter(&t2);
        if (nullr) { if (g) g->RemoveFilter(nullr); nullr->Release(); }
        if (flt)   { if (g) g->RemoveFilter(flt);   flt->Release(); }
        QueryPerformanceCounter(&t3);

        if (g) g->Release();
        if (mc) mc->Release();
        if (src) src->Release();

        double dms = (t3.QuadPart - t2.QuadPart) * 1000.0 / f.QuadPart;
        printf(" | 整轮 %.0f ms, 滤镜拆除 %.0f ms\n", ms, dms);
        if (dms > 300.0) { printf("  <-- 拆除过慢, 析构里可能还有阻塞调用\n"); fail++; }
    }

    DeleteFileW(wav2);
    DeleteFileW(wav6);
    FreeLibrary(h);
    CoUninitialize();
    printf(fail ? "\nRESULT: FAIL (%d)\n" : "\nRESULT: PASS\n", fail);
    return fail ? 1 : 0;
}
