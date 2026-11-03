// bassmax_filter.cpp  —  BassMax DirectShow 变换滤镜实现
//
// 稳定性要点 (针对"播放器偶尔卡住/卸载滤镜"):
//   * 所有指针/长度都校验, 输出写入严格限制在 pOut 的实际缓冲区内
//   * 位深按"容器位深"解析 (WAVEFORMATEXTENSIBLE 的 24-in-32 之类不会算错步长)
//   * 1~8 声道都支持, 按声道对处理; 格式不认识就明确拒绝 (VFW_E_TYPE_NOT_ACCEPTED)
//   * 音频线程内不做分配以外的阻塞操作; 分配失败返回 E_OUTOFMEMORY 而不是抛异常
#include "bassmax_filter.h"
#include "bassmax_settings.h"
#include "bassmax_tray.h"
#include <mmreg.h>   // WAVEFORMATEXTENSIBLE / WAVE_FORMAT_* 常量
#include <new>

namespace {

struct Fmt {
    bool  isFloat = false;
    int   bytes   = 2;      // 容器位深 / 8
    int   ch      = 2;
    DWORD sr      = 44100;
    bool  ok      = false;
};

// 解析媒体类型。位深一律取容器位深 (wBitsPerSample), 有效位深只影响音量刻度。
Fmt ParseFmt(const CMediaType* mt) {
    Fmt f;
    if (!mt) return f;
    if (!IsEqualGUID(*mt->Type(), MEDIATYPE_Audio)) return f;
    if (!IsEqualGUID(*mt->FormatType(), FORMAT_WaveFormatEx)) return f;
    const BYTE* p = static_cast<const BYTE*>(mt->Format());
    ULONG cb = mt->FormatLength();
    if (!p || cb < sizeof(WAVEFORMATEX)) return f;

    const WAVEFORMATEX* wf = reinterpret_cast<const WAVEFORMATEX*>(p);
    int bits = wf->wBitsPerSample;
    f.ch = wf->nChannels;
    f.sr = wf->nSamplesPerSec;

    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (cb < sizeof(WAVEFORMATEXTENSIBLE)) return f;
        const WAVEFORMATEXTENSIBLE* we = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        f.isFloat = IsEqualGUID(we->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_GUID) != FALSE;
        if (!f.isFloat && !IsEqualGUID(we->SubFormat, KSDATAFORMAT_SUBTYPE_PCM_GUID)) return f;
        bits = we->Format.wBitsPerSample;              // 容器位深
    } else if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        f.isFloat = true;
    } else if (wf->wFormatTag != WAVE_FORMAT_PCM) {
        return f;                                       // 压缩格式不接受
    }

    if (f.isFloat) {
        if (bits != 32 && bits != 64) return f;
        f.bytes = bits / 8;
    } else {
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return f;
        f.bytes = bits / 8;
    }
    if (f.ch < 1 || f.ch > 8) return f;
    // 上限 1,536,000 Hz: 覆盖 DSD256 (5,644,800/4 = 1,411,200) 及以上余量。
    // 原上限 768,000 会把 DSD256 直接判为不支持 -> 滤镜不挂载、听感等于失效。
    if (f.sr < 8000 || f.sr > 1536000) return f;
    if (wf->nBlockAlign != f.ch * f.bytes) return f;     // 步长必须自洽, 否则不接
    f.ok = true;
    return f;
}

inline float ReadSample(const BYTE* p, const Fmt& f) {
    if (f.isFloat) {
        if (f.bytes == 8) { double d; memcpy(&d, p, 8); return (float)d; }
        float v; memcpy(&v, p, 4); return v;
    }
    switch (f.bytes) {
        case 1:  return ((float)p[0] - 128.0f) / 128.0f;              // 8-bit 无符号
        case 2:  { short s;  memcpy(&s, p, 2); return (float)s / 32768.0f; }
        case 3:  { int v = p[0] | (p[1] << 8) | (p[2] << 16);
                   if (v & 0x800000) v -= 0x1000000; return (float)v / 8388608.0f; }
        case 4:  { int v; memcpy(&v, p, 4); return (float)(v / 2147483648.0); }
    }
    return 0.0f;
}

inline void WriteSample(BYTE* p, const Fmt& f, float v) {
    if (!(v == v)) v = 0.0f;                       // NaN 保护
    if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
    if (f.isFloat) {
        if (f.bytes == 8) { double d = (double)v; memcpy(p, &d, 8); }
        else              { memcpy(p, &v, 4); }
        return;
    }
    switch (f.bytes) {
        case 1:  p[0] = (BYTE)((int)(v * 127.0f) + 128); break;
        case 2:  { int s = (int)(v * 32767.0f); if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                   short s2 = (short)s; memcpy(p, &s2, 2); break; }
        case 3:  { int s = (int)(v * 8388607.0f); if (s > 8388607) s = 8388607; if (s < -8388608) s = -8388608;
                   p[0] = (BYTE)(s & 0xff); p[1] = (BYTE)((s >> 8) & 0xff); p[2] = (BYTE)((s >> 16) & 0xff); break; }
        case 4:  { double d = (double)v * 2147483520.0; int s = (int)d;
                   if (s > 2147483647) s = 2147483647; if (s < -2147483647) s = -2147483647;
                   memcpy(p, &s, 4); break; }
    }
}

} // namespace

// ---------- 滤镜实现 ----------
BassMaxFilter::BassMaxFilter(LPUNKNOWN pUnk, HRESULT* phr)
    : CTransformFilter("BassMax Audio Enhancer", pUnk, CLSID_BassMax) {
    bassmax::InitSettings();
    ApplySettings();
    // 托盘线程常驻: 除了图标, 还负责按 400ms 轮询注册表 -> 手改参数立刻生效。
    // 图标是否出现由 TrayIcon 决定 (线程里判断), 所以这里不再按开关跳过一个线程。
    bassmax::StartTray();
    m_trayOn = true;
    if (phr) *phr = S_OK;
}

BassMaxFilter::~BassMaxFilter() {
    if (m_trayOn) { bassmax::StopTray(); m_trayOn = false; }
}

CUnknown* WINAPI BassMaxFilter::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
    BassMaxFilter* p = new (std::nothrow) BassMaxFilter(pUnk, phr);
    if (!p && phr) *phr = E_OUTOFMEMORY;
    return p;
}

void BassMaxFilter::ApplyToAllCores() {
    if (m_sr == 0) return;
    for (size_t i = 0; i < m_cores.size(); ++i)
        m_cores[i].setParams(m_params, (float)m_sr);
}

// 把全局设置同步进 DSP (音频线程检测到代号变化后调用; 属性页只写注册表, 不碰 DSP)
void BassMaxFilter::ApplySettings() {
    bassmax::Settings s = bassmax::GetSettings();
    if (s.enabled) {
        m_params.bass   = s.bass;
        // 驱动: >2 声道时自动限制 (见 bassmax_filter.h 的说明)。只影响本次生效值,
        // 注册表里的用户设置保持原样, 换成 2.0 输入就恢复。
        m_params.drive  = BassMaxEffectiveDrive(s.drive, m_ch);
        m_params.preset = s.preset;
        m_params.trim9k = s.treble;      // 高音 = 9kHz 高架增益
    } else {
        // 总开关关闭: 全部归零 -> Core 内部判定 bypass, 逐样本直接返回, 不做任何处理
        m_params.bass   = 0.0f;
        m_params.drive  = 0.0f;
        m_params.preset = 0;
        m_params.trim9k = 0.0f;
    }
    ApplyToAllCores();
    InterlockedExchange(&m_gen, bassmax::SettingsGen());
}

void BassMaxFilter::SetBass(float b) {
    bassmax::Settings s = bassmax::GetSettings();
    s.bass = b;
    bassmax::SetSettings(s);
    ApplySettings();
}

void BassMaxFilter::SetDrive(float d) {
    bassmax::Settings s = bassmax::GetSettings();
    s.drive = d;
    bassmax::SetSettings(s);
    ApplySettings();
}

// ---------- 属性页 ----------
STDMETHODIMP BassMaxFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (IsEqualGUID(riid, IID_ISpecifyPropertyPages))
        return GetInterface((ISpecifyPropertyPages*)this, ppv);
    return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP BassMaxFilter::GetPages(CAUUID* pPages) {
    if (!pPages) return E_POINTER;
    pPages->pElems = (GUID*)CoTaskMemAlloc(sizeof(GUID));
    if (!pPages->pElems) { pPages->cElems = 0; return E_OUTOFMEMORY; }
    pPages->cElems = 1;
    pPages->pElems[0] = CLSID_BassMaxPage;
    return S_OK;
}

HRESULT BassMaxFilter::CheckInputType(const CMediaType* mtIn) {
    Fmt f = ParseFmt(mtIn);
    return f.ok ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
}

HRESULT BassMaxFilter::CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut) {
    Fmt a = ParseFmt(mtIn);
    Fmt b = ParseFmt(mtOut);
    if (!a.ok || !b.ok) return VFW_E_TYPE_NOT_ACCEPTED;
    if (a.isFloat != b.isFloat || a.bytes != b.bytes || a.ch != b.ch || a.sr != b.sr)
        return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtIn->Subtype(), *mtOut->Subtype())) return VFW_E_TYPE_NOT_ACCEPTED;
    return S_OK;
}

HRESULT BassMaxFilter::GetMediaType(int iPosition, CMediaType* pMediaType) {
    if (!pMediaType) return E_POINTER;
    if (iPosition < 0) return E_INVALIDARG;
    if (iPosition > 0) return VFW_S_NO_MORE_ITEMS;
    if (m_inputSet) {
        *pMediaType = m_inputMt;   // 输出与输入完全一致
        return S_OK;
    }
    // 输入未连接时的默认提案 (32-bit float 立体声 48k)
    WAVEFORMATEX wf;
    ZeroMemory(&wf, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wf.nChannels = 2; wf.nSamplesPerSec = 48000;
    wf.wBitsPerSample = 32;
    wf.nBlockAlign = 8; wf.nAvgBytesPerSec = 48000 * 8;
    pMediaType->SetType(&MEDIATYPE_Audio);
    pMediaType->SetSubtype(&MEDIASUBTYPE_IEEE_FLOAT);
    pMediaType->SetFormatType(&FORMAT_WaveFormatEx);
    pMediaType->SetFormat((BYTE*)&wf, sizeof(wf));
    return S_OK;
}

HRESULT BassMaxFilter::DecideBufferSize(IMemAllocator* pAllocator, ALLOCATOR_PROPERTIES* pProp) {
    if (!pAllocator || !pProp) return E_POINTER;
    // 以已协商的输入格式为准; 未协商时按 8 声道 32-bit 48k 给足余量
    int ch = 2, bytes = 4; DWORD sr = 48000;
    if (m_ch  > 0) ch = m_ch;
    if (m_bytes > 0) bytes = m_bytes;
    if (m_sr  > 0) sr = m_sr;
    long long need = (long long)sr * ch * bytes / 4 + 8192;   // ~0.25s
    if (need < 65536) need = 65536;
    if (need > 8*1024*1024) need = 8*1024*1024;
    pProp->cbBuffer = (long)need;
    if (pProp->cBuffers < 4) pProp->cBuffers = 4;
    pProp->cbAlign = 1;
    ALLOCATOR_PROPERTIES actual;
    HRESULT hr = pAllocator->SetProperties(pProp, &actual);
    if (FAILED(hr)) return hr;
    if (actual.cbBuffer < pProp->cbBuffer) return E_FAIL;
    return S_OK;
}

HRESULT BassMaxFilter::SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt) {
    HRESULT hr = CTransformFilter::SetMediaType(dir, pmt);
    if (FAILED(hr)) return hr;
    if (dir == PINDIR_INPUT) {
        Fmt f = ParseFmt(pmt);
        if (!f.ok) return VFW_E_TYPE_NOT_ACCEPTED;
        m_float = f.isFloat;
        m_bytes = f.bytes;
        m_ch    = f.ch;
        m_sr    = f.sr;
        m_inputMt = *pmt;
        m_inputSet = true;

        int pairs = (m_ch + 1) / 2;
        try {
            if ((int)m_cores.size() < pairs) m_cores.resize(pairs);
            if (m_scratch.size() < 4096) m_scratch.resize(4096);
        } catch (...) { return E_OUTOFMEMORY; }
        for (size_t i = 0; i < m_cores.size(); ++i) m_cores[i].reset((float)m_sr);
        ApplySettings();          // m_ch 变了 -> 重算驱动上限 (不能只 ApplyToAllCores)
    }
    return S_OK;
}

HRESULT BassMaxFilter::Transform(IMediaSample* pIn, IMediaSample* pOut) {
    if (!pIn || !pOut) return E_POINTER;
    if (m_sr == 0 || m_ch < 1 || m_bytes < 1) return E_UNEXPECTED;

    // 用户在属性页/托盘改了参数? 在音频线程里安全地重建系数
    if (bassmax::SettingsGen() != m_gen)
        ApplySettings();

    BYTE* pin = nullptr;
    BYTE* pout = nullptr;
    if (FAILED(pIn->GetPointer(&pin)) || !pin) return E_POINTER;
    if (FAILED(pOut->GetPointer(&pout)) || !pout) return E_POINTER;

    long inLen  = pIn->GetActualDataLength();
    long outMax = pOut->GetSize();
    int frameBytes = m_bytes * m_ch;
    if (frameBytes <= 0) return E_UNEXPECTED;
    long nFrames = (long)(inLen / frameBytes);
    long capFrames = (long)(outMax / frameBytes);
    if (nFrames > capFrames) nFrames = capFrames;      // 绝不越界写
    if (nFrames <= 0) { pOut->SetActualDataLength(0); return S_OK; }

    Fmt f; f.isFloat = m_float; f.bytes = m_bytes; f.ch = m_ch; f.sr = m_sr; f.ok = true;
    int pairs = (m_ch + 1) / 2;

    try {
        if ((int)m_cores.size() < pairs) {
            size_t old = m_cores.size();
            m_cores.resize(pairs);
            for (size_t i = old; i < m_cores.size(); ++i) {
                m_cores[i].setParams(m_params, (float)m_sr);
                m_cores[i].reset((float)m_sr);
            }
        }
        size_t need = (size_t)nFrames * 2;
        if (m_scratch.size() < need) m_scratch.resize(need);
    } catch (...) { return E_OUTOFMEMORY; }

    float* st = m_scratch.data();
    for (int p = 0; p < pairs; ++p) {
        int c0 = 2*p;
        int c1 = (c0 + 1 < m_ch) ? (c0 + 1) : c0;       // 奇数声道时最后一对复制同一路
        const BYTE* s0 = pin + (size_t)c0 * m_bytes;
        const BYTE* s1 = pin + (size_t)c1 * m_bytes;
        for (long i = 0; i < nFrames; ++i) {
            st[2*i]   = ReadSample(s0 + (size_t)i * frameBytes, f);
            st[2*i+1] = ReadSample(s1 + (size_t)i * frameBytes, f);
        }

        m_cores[p].process(st, (int)nFrames, (float)m_sr);

        BYTE* d0 = pout + (size_t)c0 * m_bytes;
        for (long i = 0; i < nFrames; ++i) {
            WriteSample(d0 + (size_t)i * frameBytes, f, st[2*i]);
            if (c1 != c0) WriteSample(pout + (size_t)c1 * m_bytes + (size_t)i * frameBytes, f, st[2*i+1]);
        }
    }

    pOut->SetActualDataLength((long)nFrames * frameBytes);
    REFERENCE_TIME tS, tE;
    if (SUCCEEDED(pIn->GetTime(&tS, &tE))) pOut->SetTime(&tS, &tE);
    pOut->SetSyncPoint(TRUE);
    pOut->SetDiscontinuity(pIn->IsDiscontinuity() == S_OK);
    pOut->SetPreroll(pIn->IsPreroll() == S_OK);
    return S_OK;
}
