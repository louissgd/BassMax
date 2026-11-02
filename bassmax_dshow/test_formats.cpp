// test_formats.cpp — 覆盖 LAV 可能输出的全部格式, 验证滤镜不崩、不越界、格式判断正确
#include <windows.h>
#include <mmreg.h>
#include <streams.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#include "bassmax_filter.h"

HINSTANCE g_hInst = nullptr;
DWORD     g_amPlatform = VER_PLATFORM_WIN32_NT;
OSVERSIONINFO g_osInfo = {};
CFactoryTemplate g_Templates[1] = {};
int       g_cTemplates = 0;

class FakeSample : public IMediaSample {
    LONG m_cRef = 1;
    BYTE* m_p; LONG m_size, m_len;
    REFERENCE_TIME t0 = 0, t1 = 0;
    BOOL sync = TRUE, preroll = FALSE, disc = FALSE;
public:
    FakeSample(BYTE* buf, LONG size, LONG len) : m_p(buf), m_size(size), m_len(len) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** pp) override {
        if (riid == IID_IUnknown || riid == IID_IMediaSample) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&m_cRef); if (!r) delete this; return r; }
    STDMETHODIMP GetPointer(BYTE** pp) override { *pp = m_p; return S_OK; }
    STDMETHODIMP_(LONG) GetSize() override { return m_size; }
    STDMETHODIMP GetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override { if (a)*a = t0; if (b)*b = t1; return S_OK; }
    STDMETHODIMP SetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override { if (a)t0 = *a; if (b)t1 = *b; return S_OK; }
    STDMETHODIMP IsSyncPoint() override { return sync ? S_OK : S_FALSE; }
    STDMETHODIMP SetSyncPoint(BOOL b) override { sync = b; return S_OK; }
    STDMETHODIMP IsPreroll() override { return preroll ? S_OK : S_FALSE; }
    STDMETHODIMP SetPreroll(BOOL b) override { preroll = b; return S_OK; }
    STDMETHODIMP_(LONG) GetActualDataLength() override { return m_len; }
    STDMETHODIMP SetActualDataLength(LONG l) override { m_len = l; return S_OK; }
    STDMETHODIMP GetMediaType(AM_MEDIA_TYPE** pp) override { *pp = nullptr; return E_NOTIMPL; }
    STDMETHODIMP SetMediaType(AM_MEDIA_TYPE*) override { return S_OK; }
    STDMETHODIMP IsDiscontinuity() override { return disc ? S_OK : S_FALSE; }
    STDMETHODIMP SetDiscontinuity(BOOL b) override { disc = b; return S_OK; }
    STDMETHODIMP GetMediaTime(LONGLONG*, LONGLONG*) override { return VFW_E_MEDIA_TIME_NOT_SET; }
    STDMETHODIMP SetMediaTime(LONGLONG*, LONGLONG*) override { return S_OK; }
};

static void FillTone(BYTE* buf, int frames, int ch, int bytes, bool isFloat, double sr, double amp) {
    for (int i = 0; i < frames; ++i) {
        double v = amp * sin(2*3.14159265358979*1000.0*i/sr);
        for (int c = 0; c < ch; ++c) {
            BYTE* p = buf + ((size_t)i*ch + c)*bytes;
            if (isFloat) { float f=(float)v; memcpy(p,&f,4); }
            else if (bytes == 1) p[0] = (BYTE)(128 + v*127);
            else if (bytes == 2) { short s=(short)(v*32767); memcpy(p,&s,2); }
            else if (bytes == 3) { int s=(int)(v*8388607); p[0]=(BYTE)s; p[1]=(BYTE)(s>>8); p[2]=(BYTE)(s>>16); }
            else { int s=(int)(v*2147483520.0); memcpy(p,&s,4); }
        }
    }
}

struct Case {
    const char* name;
    int ch, bytes;
    bool isFloat;
    bool ext;          // WAVEFORMATEXTENSIBLE
    int containerBits; // ext 时的容器位深
    bool useBufferHalf; // 输出缓冲故意给小
    bool expectAccept;
};

static bool BuildMT(CMediaType& mt, const Case& c, int sr) {
    if (!c.ext) {
        WAVEFORMATEX wf = {};
        wf.wFormatTag = c.isFloat ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        wf.nChannels = (WORD)c.ch; wf.nSamplesPerSec = sr;
        wf.wBitsPerSample = (WORD)(c.bytes*8);
        wf.nBlockAlign = (WORD)(c.ch*c.bytes);
        wf.nAvgBytesPerSec = sr*wf.nBlockAlign;
        mt.SetType(&MEDIATYPE_Audio);
        mt.SetSubtype(c.isFloat ? &MEDIASUBTYPE_IEEE_FLOAT : &MEDIASUBTYPE_PCM);
        mt.SetFormatType(&FORMAT_WaveFormatEx);
        mt.SetFormat((BYTE*)&wf, sizeof(wf));
        return true;
    }
    WAVEFORMATEXTENSIBLE we = {};
    we.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    we.Format.nChannels = (WORD)c.ch;
    we.Format.nSamplesPerSec = sr;
    we.Format.wBitsPerSample = (WORD)(c.bytes*8);
    we.Format.nBlockAlign = (WORD)(c.ch*c.bytes);
    we.Format.nAvgBytesPerSec = sr*we.Format.nBlockAlign;
    we.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    we.Samples.wValidBitsPerSample = (WORD)c.containerBits;
    we.dwChannelMask = 0;
    we.SubFormat = c.isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_GUID : KSDATAFORMAT_SUBTYPE_PCM_GUID;
    mt.SetType(&MEDIATYPE_Audio);
    mt.SetSubtype(c.isFloat ? &MEDIASUBTYPE_IEEE_FLOAT : &MEDIASUBTYPE_PCM);
    mt.SetFormatType(&FORMAT_WaveFormatEx);
    mt.SetFormat((BYTE*)&we, sizeof(we));
    return true;
}

int main() {
    CoInitialize(nullptr);
    const int SR = 48000, FRAMES = 1024;

    Case cases[] = {
        {"PCM 8bit mono        ",  1, 1, false, false, 0,  false, true},
        {"PCM 16bit stereo     ",  2, 2, false, false, 0,  false, true},
        {"PCM 24bit stereo     ",  2, 3, false, false, 0,  false, true},
        {"PCM 32bit stereo     ",  2, 4, false, false, 0,  false, true},
        {"EXT 24in32 stereo    ",  2, 4, false, true, 24,  false, true},
        {"EXT 16in16 stereo    ",  2, 2, false, true, 16,  false, true},
        {"float 32 stereo      ",  2, 4, true,  false, 0,  false, true},
        {"float 32 5.1 (6ch)   ",  6, 4, true,  true,  32, false, true},
        {"float 32 7.1 (8ch)   ",  8, 4, true,  true,  32, false, true},
        {"PCM 24 5.1 (6ch)     ",  6, 3, false, false, 0,  false, true},
        {"float 32 小缓冲       ",  2, 4, true,  false, 0,  true,  true},
        {"12 声道 (应拒绝)     ", 12, 4, true,  true,  32, false, false},
        {"96k float 7.1        ",  8, 4, true,  true,  32, false, true},
    };

    for (const Case& c : cases) {
        HRESULT hr = S_OK;
        BassMaxFilter* f = (BassMaxFilter*)BassMaxFilter::CreateInstance(nullptr, &hr);
        if (!f) { printf("%s CreateInstance FAILED\n", c.name); continue; }

        int sr = (c.name[0]=='9' && c.name[1]=='6') ? 96000 : SR;
        CMediaType mt; BuildMT(mt, c, sr);

        HRESULT hrChk = f->CheckInputType(&mt);
        HRESULT hrIn  = f->SetMediaType(PINDIR_INPUT, &mt);
        HRESULT hrOut = f->SetMediaType(PINDIR_OUTPUT, &mt);

        LONG frameBytes = c.ch * c.bytes;
        LONG bytes = FRAMES * frameBytes;
        LONG outBytes = c.useBufferHalf ? bytes / 3 : bytes;
        std::vector<BYTE> inBuf(bytes), outBuf(outBytes + 256);   // 尾部留哨兵
        const BYTE SENT = 0xA5;
        for (int i = outBytes; i < (int)outBuf.size(); ++i) outBuf[i] = SENT;

        FillTone(inBuf.data(), FRAMES, c.ch, c.bytes, c.isFloat, sr, 0.4);

        FakeSample* pIn = new FakeSample(inBuf.data(), bytes, bytes);
        FakeSample* pOut = new FakeSample(outBuf.data(), outBytes, outBytes);
        HRESULT hrT = f->Transform(pIn, pOut);
        LONG outLen = pOut->GetActualDataLength();

        bool sentinelOk = true;
        for (int i = outBytes; i < (int)outBuf.size(); ++i) if (outBuf[i] != SENT) { sentinelOk = false; break; }

        double peak = 0; bool anyNan = false;
        for (int i = 0; i < outLen/frameBytes; ++i) {
            for (int ch = 0; ch < c.ch; ++ch) {
                const BYTE* p = outBuf.data() + (size_t)i*frameBytes + ch*c.bytes;
                double v = 0;
                if (c.isFloat) { float t; memcpy(&t,p,4); v=t; if (t!=t) anyNan=true; }
                else if (c.bytes==1) v = (p[0]-128)/128.0;
                else if (c.bytes==2) { short s; memcpy(&s,p,2); v=s/32768.0; }
                else if (c.bytes==3) { int s=p[0]|(p[1]<<8)|(p[2]<<16); if(s&0x800000)s-=0x1000000; v=s/8388608.0; }
                else { int s; memcpy(&s,p,4); v=s/2147483648.0; }
                if (fabs(v) > peak) peak = fabs(v);
            }
        }

        printf("%s check=0x%08lX in=0x%08lX out=0x%08lX tr=0x%08lX len=%ld/%ld peak=%.3f 越界=%s NaN=%s\n",
               c.name, (unsigned long)hrChk, (unsigned long)hrIn, (unsigned long)hrOut,
               (unsigned long)hrT, outLen, outBytes, peak,
               sentinelOk ? "无" : "有!", anyNan ? "有!" : "无");

        pIn->Release(); pOut->Release();
        f->Release();
    }

    // ---- 压力: 5.1 float, 连续 2000 个缓冲 ----
    {
        Case c = {"5.1 stress", 6, 4, true, true, 32, false, true};
        HRESULT hr = S_OK;
        BassMaxFilter* f = (BassMaxFilter*)BassMaxFilter::CreateInstance(nullptr, &hr);
        CMediaType mt; BuildMT(mt, c, SR);
        f->SetMediaType(PINDIR_INPUT, &mt);
        f->SetMediaType(PINDIR_OUTPUT, &mt);
        LONG frameBytes = c.ch*c.bytes, bytes = FRAMES*frameBytes;
        std::vector<BYTE> inBuf(bytes), outBuf(bytes);
        FillTone(inBuf.data(), FRAMES, c.ch, c.bytes, true, SR, 0.4);
        DWORD t0 = GetTickCount();
        int bad = 0;
        for (int k = 0; k < 2000; ++k) {
            FakeSample* pIn = new FakeSample(inBuf.data(), bytes, bytes);
            FakeSample* pOut = new FakeSample(outBuf.data(), bytes, bytes);
            if (FAILED(f->Transform(pIn, pOut))) ++bad;
            pIn->Release(); pOut->Release();
        }
        DWORD dt = GetTickCount() - t0;
        double audioSec = 2000.0*FRAMES/SR;
        printf("压力: 2000 缓冲 = %.1f s 音频, 耗时 %lu ms (%.1f%% 单核, 失败 %d)\n",
               audioSec, (unsigned long)dt, dt/1000.0/audioSec*100.0, bad);
        f->Release();
    }

    CoUninitialize();
    return 0;
}
