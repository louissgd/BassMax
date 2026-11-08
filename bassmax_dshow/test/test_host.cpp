// test_host.cpp — 用真实 DirectShow 图验证 BassMax 滤镜
//
// 1.0.6 起一个 .ax 只有一个 CLSID:
//   {C41F7D22-...-C9B1}  CTransInPlaceFilter  "BassMax Audio Enhancer"
//
// 不依赖注册表: 直接 LoadLibrary .ax + DllGetClassObject, 手工 AddFilter。
// 跑 5 轮 (2ch / 6ch 交替), 每轮验证:
//   1) 图能接通 (WAV Parser -> 本滤镜 -> Null Renderer), 输入/输出都不报错
//   2) 运行中请求 6ch/48k/float32: 输入引脚 QueryAccept 必须 S_OK
//      (旧版 CTransformFilter + 精确 CheckTransform 会 S_FALSE -> 切音轨失败)
//   3) 整段 2 秒音频真实推流: 必须跑到 EC_COMPLETE 且没有 EC_ERRORABORT
//      (这一条才会走 DecideBufferSize / 拷贝 / Transform / 下游 Receive 全链路:
//       块长算错、拷贝越界、Transform 返回失败, 都会在这里变成中断或错误事件)
//   4) 拆除/重建 5 轮, 每轮滤镜析构 < 300ms (旧版析构里 WaitForSingleObject(2000) 会露馅)
//
// 注意: 这里刻意"手工往引脚里塞样本"的用例已经被去掉 —— Null Renderer 同一时刻
// 只接受一个采样, 塞样本会撞上它自己的 flush/EOS 状态机, 结果忽好忽坏, 反而掩盖
// 真问题。真实推流才是播放器实际走的路径。
//
// 必须用 x64 构建: 32 位进程加载 64 位 .ax 会直接 ERROR_BAD_EXE_FORMAT(193)。
//   构建: bash test/build_test.sh
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <mmreg.h>
#include <stdio.h>
#include <math.h>

static const GUID kCLSID_Tip = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1}
};
static const GUID kCLSID_NullRenderer = {
    0xc1f400a4, 0x3f08, 0x11d3, {0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}
};

// 生成 N 秒 48k/16bit N 声道 WAV
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

static void FreeMt(AM_MEDIA_TYPE& mt) {
    if (mt.cbFormat && mt.pbFormat) CoTaskMemFree(mt.pbFormat);
    if (mt.pUnk) { mt.pUnk->Release(); mt.pUnk = nullptr; }
    ZeroMemory(&mt, sizeof(mt));
}

// float32 / N 声道的 WAVEFORMATEXTENSIBLE
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

static IBaseFilter* CreateByClsid(HMODULE h, const GUID& clsid) {
    typedef HRESULT (STDAPICALLTYPE *PFN_GCO)(REFCLSID, REFIID, void**);
    PFN_GCO gco = (PFN_GCO)GetProcAddress(h, "DllGetClassObject");
    if (!gco) { printf("    !! DllGetClassObject 找不到\n"); return nullptr; }
    IClassFactory* cf = nullptr;
    HRESULT hr = gco(clsid, IID_IClassFactory, (void**)&cf);
    if (FAILED(hr)) { printf("    !! DllGetClassObject hr=0x%08X\n", hr); return nullptr; }
    IBaseFilter* p = nullptr;
    hr = cf->CreateInstance(nullptr, IID_IBaseFilter, (void**)&p);
    cf->Release();
    if (FAILED(hr)) printf("    !! CreateInstance hr=0x%08X\n", hr);
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

// 跑到 EC_COMPLETE 或超时; 返回 0 = 正常播完, 其它 = 失败原因
static const char* WaitComplete(IMediaEvent* me, DWORD timeoutMs, HRESULT* badHr) {
    DWORD t0 = GetTickCount();
    *badHr = S_OK;
    for (;;) {
        long ev = 0;
        LONG_PTR p1 = 0, p2 = 0;
        HRESULT hr = me->GetEvent(&ev, &p1, &p2, 200);
        if (SUCCEEDED(hr)) {
            if (ev == EC_COMPLETE) { me->FreeEventParams(ev, p1, p2); return nullptr; }
            if (ev == EC_ERRORABORT || ev == EC_USERABORT ||
                ev == EC_STREAM_ERROR_STOPPED) {
                *badHr = (HRESULT)p1;
                me->FreeEventParams(ev, p1, p2);
                return "播放被中断";
            }
            me->FreeEventParams(ev, p1, p2);
        } else if (hr != E_ABORT) {
            // 没有事件可用不是错误, 继续等
        }
        if (GetTickCount() - t0 > timeoutMs) return "超时没有等到 EC_COMPLETE";
    }
}

// 返回失败计数
static int RunClsid(HMODULE h, const GUID& clsid, const char* label,
                    const wchar_t* wav2, const wchar_t* wav6) {
    int fail = 0;
    printf("\n===== %s =====\n", label);

    for (int round = 0; round < 5; ++round) {
        LARGE_INTEGER f, t0, t1, t2, t3;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);

        IGraphBuilder* g = nullptr;
        IMediaControl* mc = nullptr;
        IMediaEvent*   me = nullptr;
        IBaseFilter* src = nullptr, *flt = nullptr, *nullr = nullptr;
        IPin *pinOut = nullptr, *pinIn = nullptr, *pinFOut = nullptr, *pinRIn = nullptr;

        CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IGraphBuilder, (void**)&g);
        g->QueryInterface(IID_IMediaControl, (void**)&mc);
        g->QueryInterface(IID_IMediaEvent, (void**)&me);
        const wchar_t* wav = (round % 2) ? wav6 : wav2;
        g->AddSourceFilter(wav, L"Src", &src);

        flt = CreateByClsid(h, clsid);
        if (!flt) { fail++; goto cleanup; }

        // 滤镜要能打开属性页 (ISpecifyPropertyPages)
        {
            ISpecifyPropertyPages* sp = nullptr;
            HRESULT hq = flt->QueryInterface(IID_ISpecifyPropertyPages, (void**)&sp);
            if (FAILED(hq)) { printf("  !! 属性页接口 QI 失败 0x%08X\n", hq); fail++; }
            else sp->Release();
        }

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
            printf("round %d (%dch): connect in=0x%08X out=0x%08X", round,
                   (round % 2) ? 6 : 2, h1, h2);
            if (FAILED(h1) || FAILED(h2)) { printf(" <-- 连不上\n"); fail++; goto cleanup; }

            // (2) 运行中请求 6ch/48k/float32
            {
                AM_MEDIA_TYPE mt6; MakeExtFloat(mt6, 6, 48000, 32);
                HRESULT qa = pinIn->QueryAccept(&mt6);
                FreeMt(mt6);
                printf(" | QueryAccept(6ch/float)=0x%08X", qa);
                if (qa != S_OK) { printf(" <-- 拒绝格式变更, 切轨会失败\n"); fail++; }
            }

            // (3) 真实推流: 2 秒音频必须完整播完且不报错
            HRESULT hrRun = mc->Run();
            if (FAILED(hrRun)) { printf(" | Run=0x%08X <-- 起播失败\n", hrRun); fail++; }
            else {
                HRESULT bad = S_OK;
                const char* why = WaitComplete(me, 6000, &bad);
                if (why) { printf(" | 播放=失败: %s (0x%08X)\n", why, bad); fail++; }
                else     { printf(" | 播放=完整"); }
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

        if (me) me->Release();
        if (g) g->Release();
        if (mc) mc->Release();
        if (src) src->Release();

        double dms = (t3.QuadPart - t2.QuadPart) * 1000.0 / f.QuadPart;
        printf(" | 整轮 %.0f ms, 滤镜拆除 %.0f ms\n", ms, dms);
        if (dms > 300.0) { printf("  <-- 拆除过慢, 析构里可能还有阻塞调用\n"); fail++; }
    }
    return fail;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* axPath = (argc > 1) ? argv[1] : L"bassmax_dshow.ax";
    SetConsoleOutputCP(CP_UTF8);          // 源码是 UTF-8 (/utf-8), 控制台直接按 UTF-8 输出
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE h = LoadLibraryW(axPath);
    if (!h) {
        printf("LoadLibrary 失败 %lu (%ls)\n", GetLastError(), axPath);
        printf("提示: 32 位宿主加载 64 位 .ax 会返回 193 (ERROR_BAD_EXE_FORMAT)\n");
        return 1;
    }

    wchar_t wav2[MAX_PATH], wav6[MAX_PATH];
    GetTempPathW(MAX_PATH, wav2);
    GetTempPathW(MAX_PATH, wav6);
    wcscat_s(wav2, L"bassmax_test2.wav");
    wcscat_s(wav6, L"bassmax_test6.wav");
    MakeWav(wav2, 2, 48000, 2);
    MakeWav(wav6, 6, 48000, 2);

    int fail = 0;
    fail += RunClsid(h, kCLSID_Tip, "BassMax Audio Enhancer (CTransInPlace)", wav2, wav6);

    DeleteFileW(wav2);
    DeleteFileW(wav6);
    FreeLibrary(h);
    CoUninitialize();
    printf(fail ? "\nRESULT: FAIL (%d)\n" : "\nRESULT: PASS\n", fail);
    return fail ? 1 : 0;
}
