// host.cpp — bassmax_decoder.ax 的真实播放烟测宿主 (x64)
//
// 目标: 验证"解码器壳"架构本身是否成立
//   [File Source] --(压缩)--> [BassMax Audio Decoder.In]
//   [BassMax Audio Decoder.Out] --(PCM)--> [Null Renderer]
//  succeed  = 图能连上 + 能 Run + 收到 EC_COMPLETE + 日志里出现 LAV 输出 PCM / DSP 处理
//
// 用法: host.exe <音频文件> [--slow]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <stdio.h>
#include <wchar.h>

static const GUID CLSID_BassMaxDecoder = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb4}
};
static const GUID kCLSID_NullRenderer = {
    0xC1F400A4, 0x3F08, 0x11D3, {0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37}
};
static const GUID kCLSID_AsyncReader = {
    0xE436EBB5, 0x524F, 0x11CE, {0x9F,0x53,0x00,0x20,0xAF,0x0B,0xA7,0x70}
};

static void FreeMt(AM_MEDIA_TYPE& mt) {
    if (mt.cbFormat && mt.pbFormat) CoTaskMemFree(mt.pbFormat);
    if (mt.pUnk) mt.pUnk->Release();
    ZeroMemory(&mt, sizeof(mt));
}
#define FreeMediaType FreeMt

static const char* HrStr(HRESULT hr, char* b) {
    if (hr == S_OK) { strcpy_s(b, 64, "S_OK"); return b; }
    wsprintfA(b, "0x%08lX", (unsigned long)hr); return b;
}

static void DumpPin(const char* tag, IPin* p) {
    char h[64];
    if (!p) { printf("  %s <null pin>\n", tag); return; }
    PIN_DIRECTION d = PINDIR_INPUT;
    p->QueryDirection(&d);
    WCHAR nm[128] = L"";
    PIN_INFO pi = {};
    if (SUCCEEDED(p->QueryPinInfo(&pi))) { if (pi.achName) wcsncpy_s(nm, pi.achName, _TRUNCATE); if (pi.pFilter) pi.pFilter->Release(); }
    AM_MEDIA_TYPE mt = {};
    HRESULT hr = p->ConnectionMediaType(&mt);
    printf("  %-14s dir=%s name=%ls conn=0x%08lX", tag, d == PINDIR_INPUT ? "IN " : "OUT", nm, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        printf(" major=%08lX sub=%08lX fmt=%08lX cb=%lu",
               (unsigned long)mt.majortype.Data1, (unsigned long)mt.subtype.Data1,
               (unsigned long)mt.formattype.Data1, (unsigned long)mt.cbFormat);
        if (mt.formattype == FORMAT_WaveFormatEx && mt.cbFormat >= sizeof(WAVEFORMATEX) && mt.pbFormat) {
            const WAVEFORMATEX* w = (const WAVEFORMATEX*)mt.pbFormat;
            printf(" tag=%u ch=%u sr=%lu bits=%u align=%u", w->wFormatTag, w->nChannels,
                   (unsigned long)w->nSamplesPerSec, w->wBitsPerSample, w->nBlockAlign);
        }
        FreeMediaType(mt);
    }
    printf("\n");
    (void)HrStr(hr, h);
}

static IPin* FindPin(IBaseFilter* f, PIN_DIRECTION dir, int index = 0) {
    IEnumPins* e = nullptr;
    if (FAILED(f->EnumPins(&e)) || !e) return nullptr;
    IPin* p = nullptr; int n = 0;
    while (e->Next(1, &p, nullptr) == S_OK && p) {
        PIN_DIRECTION d;
        if (SUCCEEDED(p->QueryDirection(&d)) && d == dir) {
            if (n++ == index) { e->Release(); return p; }
        }
        p->Release(); p = nullptr;
    }
    e->Release();
    return nullptr;
}

static void DumpGraphFilters(IGraphBuilder* g, const char* tag) {
    IEnumFilters* e = nullptr;
    if (FAILED(g->EnumFilters(&e)) || !e) return;
    printf("--- %s: Filters currently loaded ---\n", tag);
    IBaseFilter* f = nullptr;
    while (e->Next(1, &f, nullptr) == S_OK && f) {
        FILTER_INFO fi = {};
        if (SUCCEEDED(f->QueryFilterInfo(&fi))) {
            wprintf(L"  - %ls\n", fi.achName);
            if (fi.pGraph) fi.pGraph->Release();
        }
        f->Release(); f = nullptr;
    }
    e->Release();
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { printf("usage: host.exe <audio-file> [--slow]\n"); return 2; }
    const bool seek   = (argc >= 3 && _wcsicmp(argv[2], L"--seek") == 0);
    const bool swtch  = (argc >= 4 && _wcsicmp(argv[2], L"--switch") == 0);
    const bool slow   = (argc >= 3 && (_wcsicmp(argv[2], L"--slow") == 0 || seek || swtch));
    printf("FILE: %ls\n", argv[1]);

    CoInitialize(nullptr);
    HRESULT hr = S_OK;
    IGraphBuilder* g = nullptr;
    IMediaControl* mc = nullptr;
    IMediaEvent*   me = nullptr;
    IMediaSeeking* ms = nullptr;
    IBaseFilter *src = nullptr, *dec = nullptr, *nullR = nullptr;

#define CHECK(expr) do { hr = (expr); if (FAILED(hr)) { \
    char b[64]; printf("FAIL %s -> %s (line %d)\n", #expr, HrStr(hr,b), __LINE__); goto done; } } while (0)

    CHECK(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&g));
    CHECK(g->QueryInterface(IID_IMediaControl, (void**)&mc));
    CHECK(g->QueryInterface(IID_IMediaEvent,   (void**)&me));
    CHECK(g->QueryInterface(IID_IMediaSeeking, (void**)&ms));

    // 不用参考时钟: 让整段音频尽快推完 (Null Renderer 不渲染, 不需要按真实时间等待)
    if (!slow) {
        IMediaFilter* mf = nullptr;
        if (SUCCEEDED(g->QueryInterface(IID_IMediaFilter, (void**)&mf)) && mf) {
            mf->SetSyncSource(nullptr);
            mf->Release();
        }
    }

    CHECK(CoCreateInstance(kCLSID_AsyncReader, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&src));
    {
        IFileSourceFilter* fs = nullptr;
        CHECK(src->QueryInterface(IID_IFileSourceFilter, (void**)&fs));
        hr = fs->Load(argv[1], nullptr);
        fs->Release();
        if (FAILED(hr)) { printf("FAIL IFileSourceFilter::Load -> 0x%08lX\n", (unsigned long)hr); goto done; }
    }
    CHECK(g->AddFilter(src, L"File Source (Async)"));

    CHECK(CoCreateInstance(CLSID_BassMaxDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&dec));
    CHECK(g->AddFilter(dec, L"BassMax Audio Decoder (LAV Wrapper)"));

    CHECK(CoCreateInstance(kCLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&nullR));
    CHECK(g->AddFilter(nullR, L"Null Renderer"));

    IPin *srcOut = nullptr, *decIn = nullptr, *decOut = nullptr, *nullIn = nullptr;
    srcOut = FindPin(src, PINDIR_OUTPUT);
    decIn  = FindPin(dec, PINDIR_INPUT);
    decOut = FindPin(dec, PINDIR_OUTPUT);
    nullIn = FindPin(nullR, PINDIR_INPUT);
    printf("pins: srcOut=%p decIn=%p decOut=%p nullIn=%p\n", srcOut, decIn, decOut, nullIn);
    if (!srcOut || !decIn || !decOut || !nullIn) { printf("FAIL 引脚枚举不全\n"); goto done; }

    // 智能连接: m4a/mp3 会自动插入 splitter / demux, 输出压缩音频给我们的输入引脚
    printf("\n[1] Connect(FileSource.Out -> BMD.In)\n");
    hr = g->Connect(srcOut, decIn);
    printf("    -> 0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr)) goto dump;
    DumpPin("BMD.In", decIn);

    printf("\n[2] Connect(BMD.Out -> NullRenderer.In)\n");
    hr = g->Connect(decOut, nullIn);
    printf("    -> 0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr)) goto dump;
    DumpPin("BMD.Out", decOut);

dump:
    DumpGraphFilters(g, "after connect");

    if (SUCCEEDED(hr)) {
        printf("\n[3] Run\n");
        hr = mc->Run();
        printf("    Run -> 0x%08lX\n", (unsigned long)hr);
        if (SUCCEEDED(hr) && swtch) {
            // 模拟切音轨: 保持输出侧连接不变, 只把输入侧重连到另一个压缩格式,
            // 验证 BuildPrivateGraph 重建 + DSP 重新 SetFormat + 时间戳继续转发。
            Sleep(600);
            mc->Stop();
            g->Disconnect(srcOut);
            g->Disconnect(decIn);
            IBaseFilter* src2 = nullptr;
            hr = CoCreateInstance(kCLSID_AsyncReader, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IBaseFilter, (void**)&src2);
            if (SUCCEEDED(hr)) {
                IFileSourceFilter* fs2 = nullptr;
                hr = src2->QueryInterface(IID_IFileSourceFilter, (void**)&fs2);
                if (SUCCEEDED(hr)) { hr = fs2->Load(argv[3], nullptr); fs2->Release(); }
                if (SUCCEEDED(hr)) hr = g->AddFilter(src2, L"File Source 2 (Async)");
            }
            printf("    第二个源文件 Load+AddFilter -> 0x%08lX\n", (unsigned long)hr);
            IPin* out2 = SUCCEEDED(hr) ? FindPin(src2, PINDIR_OUTPUT) : nullptr;
            if (SUCCEEDED(hr) && out2) hr = g->Connect(out2, decIn);
            printf("    重连 (源2 -> BMD.In) -> 0x%08lX\n", (unsigned long)hr);
            if (SUCCEEDED(hr)) DumpPin("BMD.In(new)", decIn);
            if (SUCCEEDED(hr)) hr = mc->Run();
            Sleep(1800);
            bool aborted = false;
            long ev; LONG_PTR a, b;
            while (SUCCEEDED(me->GetEvent(&ev, &a, &b, 0))) {
                printf("    EVENT %ld a=0x%08lX b=0x%08lX\n", ev, (unsigned long)a, (unsigned long)b);
                if (ev == EC_ERRORABORT || ev == EC_USERABORT) aborted = true;
                me->FreeEventParams(ev, a, b);
            }
            mc->Stop();
            printf("    ABORT=%d, 切轨后继续播放 1.8s %s\n", (int)aborted, aborted ? "失败" : "正常");
            if (aborted) hr = E_FAIL;
            if (out2) out2->Release();
            if (src2) src2->Release();
        } else if (SUCCEEDED(hr) && seek) {
            Sleep(1200);
            REFERENCE_TIME dur = 0;
            ms->GetDuration(&dur);
            REFERENCE_TIME pos = dur / 2;
            HRESULT hs = ms->SetPositions(&pos, AM_SEEKING_AbsolutePositioning,
                                          nullptr, AM_SEEKING_NoPositioning);
            printf("    duration=%lld  Seek(50%%=%lld) -> 0x%08lX\n",
                   (long long)dur, (long long)pos, (unsigned long)hs);
            if (FAILED(hs)) hr = hs;
            Sleep(1800);
            bool aborted = false;
            long ev; LONG_PTR a, b;
            while (SUCCEEDED(me->GetEvent(&ev, &a, &b, 0))) {
                printf("    EVENT %ld a=0x%08lX b=0x%08lX\n", ev, (unsigned long)a, (unsigned long)b);
                if (ev == EC_ERRORABORT || ev == EC_USERABORT) aborted = true;
                me->FreeEventParams(ev, a, b);
            }
            mc->Stop();
            printf("    ABORT=%d, seek 后继续播放 1.8s %s\n", (int)aborted, aborted ? "失败" : "正常");
            if (aborted) hr = E_FAIL;
        } else if (SUCCEEDED(hr)) {
            long code = 0; LONG_PTR p1 = 0, p2 = 0;
            bool complete = false, aborted = false;
            for (int i = 0; i < 40; ++i) {
                if (me->WaitForCompletion(250, &code) == S_OK) {
                    if (code == EC_COMPLETE) complete = true;
                    if (code == EC_ERRORABORT || code == EC_USERABORT) aborted = true;
                    break;
                }
            }
            printf("    EC_COMPLETE=%d ABORT=%d code=%ld\n", (int)complete, (int)aborted, code);
            mc->Stop();
            // 打印所有事件, 便于看到 EC_ERRORABORT / VFW_E_*
            long ev; LONG_PTR a, b;
            while (SUCCEEDED(me->GetEvent(&ev, &a, &b, 0))) {
                printf("    EVENT %ld a=0x%08lX b=0x%08lX\n", ev, (unsigned long)a, (unsigned long)b);
                me->FreeEventParams(ev, a, b);
            }
            if (!complete || aborted) hr = E_FAIL;
        }
    }

done:
    printf("\nRESULT: %s\n", SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (me) me->Release();
    if (ms) ms->Release();
    if (mc) mc->Release();
    if (nullR) nullR->Release();
    if (dec) dec->Release();
    if (src) src->Release();
    if (g) g->Release();
    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}
