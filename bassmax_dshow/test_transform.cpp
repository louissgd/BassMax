// 直接调用滤镜 Transform, 验证 16-bit PCM / float 两种格式的处理结果
#include <windows.h>
#include <mmreg.h>
#include <streams.h>
#include <math.h>
#include <stdio.h>
#include "bassmax_filter.h"

// baseclasses 需要的全局 (dllmain.obj 被排除, 这里自行提供)
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

static double rmsF(const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += (double)x[i] * x[i]; return sqrt(s / n); }

int main() {
    CoInitialize(nullptr);
    const int SR = 44100, FRAMES = 8192, CH = 2;
    const double AMP = 0.5, FREQ = 1000.0;

    // ---- 1) 16-bit PCM ----
    {
        HRESULT hr = S_OK;
        BassMaxFilter* f = (BassMaxFilter*)BassMaxFilter::CreateInstance(nullptr, &hr);
        if (!f) { printf("CreateInstance FAILED\n"); return 1; }

        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = CH; wf.nSamplesPerSec = SR;
        wf.wBitsPerSample = 16; wf.nBlockAlign = CH * 2; wf.nAvgBytesPerSec = SR * CH * 2;

        CMediaType mt;
        mt.SetType(&MEDIATYPE_Audio);
        mt.SetSubtype(&MEDIASUBTYPE_PCM);
        mt.SetFormatType(&FORMAT_WaveFormatEx);
        mt.SetFormat((BYTE*)&wf, sizeof(wf));

        f->SetMediaType(PINDIR_INPUT, &mt);
        f->SetMediaType(PINDIR_OUTPUT, &mt);

        LONG bytes = FRAMES * CH * 2;
        BYTE* inBuf = new BYTE[bytes];
        BYTE* outBuf = new BYTE[bytes];
        ZeroMemory(outBuf, bytes);
        short* s = (short*)inBuf;
        for (int i = 0; i < FRAMES; ++i) {
            short v = (short)(32767.0 * AMP * sin(2 * 3.14159265358979 * FREQ * i / SR));
            s[i * 2] = v; s[i * 2 + 1] = v;
        }

        FakeSample* pIn = new FakeSample(inBuf, bytes, bytes);
        FakeSample* pOut = new FakeSample(outBuf, bytes, bytes);
        HRESULT hrT = f->Transform(pIn, pOut);
        printf("[16bit] Transform hr=0x%08lX, 输出长度=%ld\n", (unsigned long)hrT, pOut->GetActualDataLength());

        double rIn = 0, rOut = 0;
        short* o = (short*)outBuf;
        for (int i = 0; i < FRAMES * 2; ++i) {
            rIn += (double)s[i] * s[i];
            rOut += (double)o[i] * o[i];
        }
        rIn = sqrt(rIn / (FRAMES * 2)); rOut = sqrt(rOut / (FRAMES * 2));
        printf("[16bit] inRMS=%.1f outRMS=%.1f gain=%.2f dB\n", rIn, rOut, 20 * log10(rOut / rIn));
        printf("[16bit] 前 8 个输出样本: %d %d %d %d %d %d %d %d\n",
               o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7]);

        pIn->Release(); pOut->Release();
        delete[] inBuf; delete[] outBuf;
        f->Release();
    }

    // ---- 2) 32-bit float ----
    {
        HRESULT hr = S_OK;
        BassMaxFilter* f = (BassMaxFilter*)BassMaxFilter::CreateInstance(nullptr, &hr);

        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; wf.nChannels = CH; wf.nSamplesPerSec = SR;
        wf.wBitsPerSample = 32; wf.nBlockAlign = CH * 4; wf.nAvgBytesPerSec = SR * CH * 4;
        wf.cbSize = 0;

        CMediaType mt;
        mt.SetType(&MEDIATYPE_Audio);
        mt.SetSubtype(&MEDIASUBTYPE_IEEE_FLOAT);
        mt.SetFormatType(&FORMAT_WaveFormatEx);
        mt.SetFormat((BYTE*)&wf, sizeof(wf));

        f->SetMediaType(PINDIR_INPUT, &mt);
        f->SetMediaType(PINDIR_OUTPUT, &mt);

        LONG bytes = FRAMES * CH * 4;
        BYTE* inBuf = new BYTE[bytes];
        BYTE* outBuf = new BYTE[bytes];
        ZeroMemory(outBuf, bytes);
        float* s = (float*)inBuf;
        for (int i = 0; i < FRAMES; ++i) {
            float v = (float)(AMP * sin(2 * 3.14159265358979 * FREQ * i / SR));
            s[i * 2] = v; s[i * 2 + 1] = v;
        }

        FakeSample* pIn = new FakeSample(inBuf, bytes, bytes);
        FakeSample* pOut = new FakeSample(outBuf, bytes, bytes);
        HRESULT hrT = f->Transform(pIn, pOut);
        printf("[float] Transform hr=0x%08lX, 输出长度=%ld\n", (unsigned long)hrT, pOut->GetActualDataLength());

        float* o = (float*)outBuf;
        printf("[float] inRMS=%.4f outRMS=%.4f gain=%.2f dB\n", rmsF(s, FRAMES * 2), rmsF(o, FRAMES * 2),
               20 * log10(rmsF(o, FRAMES * 2) / rmsF(s, FRAMES * 2)));

        pIn->Release(); pOut->Release();
        delete[] inBuf; delete[] outBuf;
        f->Release();
    }

    CoUninitialize();
    return 0;
}
