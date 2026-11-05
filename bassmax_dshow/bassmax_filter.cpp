// bassmax_filter.cpp  —  BassMax DirectShow 变换滤镜实现 (CTransInPlaceFilter)
//
// 稳定性要点:
//   * 就地变换: 输入格式 = 输出格式, 不需要输出分配器 / 不需要拷贝样本
//   * 任何"改不动"的格式 (DTS/AC3 passthrough、>8 声道、未知位深) 一律原样透传,
//     Transform 永远返回 S_OK —— 音效滤镜绝不能把播放器的图搞崩
//   * 运行中格式变更 (LAV 切音轨) 有两条入口都处理:
//       1) 输入引脚 SetMediaType -> SetFormatFromType
//       2) 采样自带 AM_MEDIA_TYPE (IMediaSample::GetMediaType) -> 同样重建
//   * 音频线程内不阻塞、不抛异常; 分配失败就降级成透传, 不返回 E_OUTOFMEMORY
#include "bassmax_filter.h"
#include "bassmax_settings.h"
#include "bassmax_tray.h"
#include <mmreg.h>   // WAVEFORMATEXTENSIBLE / WAVE_FORMAT_* 常量
#include <new>

namespace {

inline float ReadSample(const BYTE* p, const BassMaxFmt& f) {
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

inline void WriteSample(BYTE* p, const BassMaxFmt& f, float v) {
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

// 解析媒体类型。位深一律取容器位深 (wBitsPerSample), 有效位深只影响音量刻度。
BassMaxFmt BassMaxParseFmt(const CMediaType* mt) {
    BassMaxFmt f;
    if (!mt || !mt->IsValid()) return f;
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
        return f;                                       // 压缩格式不接受 (交给播放器透传)
    }

    if (f.isFloat) {
        if (bits != 32 && bits != 64) return f;
    } else {
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return f;
    }
    if (bits <= 0) return f;
    f.bytes = bits / 8;
    if (f.bytes < 1) return f;

    if (f.ch < 1 || f.ch > 8) return f;
    // 上限 1,536,000 Hz: 覆盖 DSD256 (5,644,800/4 = 1,411,200) 及以上余量。
    // 原上限 768,000 会把 DSD256 直接判为不支持 -> 滤镜不挂载、听感等于失效。
    if (f.sr < 8000 || f.sr > 1536000) return f;

    // 步长自洽检查: 正常是 ch*bytes; 少数解码器会给 24-in-32 这类"容器填充",
    // 只要 nBlockAlign/ch 落在 {1,2,3,4,8} 就按容器的实际字节数走, 不再一刀切拒绝 ——
    // 一刀切拒绝的后果是 LAV 切到这种音轨时连接失败, 整条链断掉。
    if (wf->nBlockAlign != (WORD)(f.ch * f.bytes)) {
        if (wf->nBlockAlign == 0 || wf->nBlockAlign % f.ch != 0) return f;
        int pad = wf->nBlockAlign / f.ch;
        if (pad != 1 && pad != 2 && pad != 3 && pad != 4 && pad != 8) return f;
        if (f.isFloat && pad != 4 && pad != 8) return f;
        f.bytes = pad;
    }
    f.ok = true;
    return f;
}

// ---------- 滤镜实现 ----------
BassMaxFilter::BassMaxFilter(LPUNKNOWN pUnk, HRESULT* phr)
    : CTransInPlaceFilter("BassMax Audio Enhancer", pUnk, CLSID_BassMax, phr,
                          /*bModifiesData=*/TRUE) {
    bassmax::InitSettings();
    ApplySettings();
    // 托盘线程常驻: 除了图标, 还负责按 400ms 轮询注册表 -> 手改参数立刻生效。
    // 图标是否出现由 TrayIcon 决定 (线程里判断), 所以这里不再按开关跳过一个线程。
    bassmax::StartTray();
    m_trayOn = true;
    if (phr) *phr = S_OK;
}

BassMaxFilter::~BassMaxFilter() {
    // StopTray 绝不阻塞: 析构可能跑在播放器拆图的线程上, 任何等待都会直接卡住
    // 播放器 (切音轨 = 拆图 + 重建, 这条路径上等 2 秒等于画面停 2 秒)。
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

bool BassMaxFilter::ReserveScratch(size_t frames) {
    size_t need = frames * 2;
    if (m_scratch.size() >= need) return true;
    try { m_scratch.resize(need); } catch (...) { return false; }
    return true;
}

void BassMaxFilter::RebuildCores() {
    if (m_ch < 1 || m_sr == 0) return;
    int pairs = (m_ch + 1) / 2;
    if (pairs < 1) pairs = 1;
    try {
        if ((int)m_cores.size() < pairs) m_cores.resize((size_t)pairs);
    } catch (...) {
        m_processable = false;      // 分配不出来就彻底透传, 不带病处理
        return;
    }
    for (int i = 0; i < pairs; ++i) {
        m_cores[i].setParams(m_params, (float)m_sr);   // 内部会 reset 状态
        m_cores[i].reset((float)m_sr);
    }
    ReserveScratch(16384);          // 预分配, 音频线程里尽量不再扩容
}

bool BassMaxFilter::SetFormatFromType(const CMediaType* mt) {
    BassMaxFmt f = BassMaxParseFmt(mt);
    if (!f.ok) {
        // 不认识的格式 (压缩 passthrough / 声道过多 / 位深怪): 清状态走透传。
        // 这里绝不返回错误 —— 否则 LAV 切到这种音轨时连接直接失败, 图就没了。
        m_processable = false;
        m_sr = 0;
        m_ch = 0;
        m_frameBytes = 0;
        InterlockedExchange(&m_gen, -1);   // 下次可处理时重新套一遍设置
        return false;
    }
    const bool changed = (f.ch != m_ch) || (f.sr != m_sr) ||
                         (f.bytes != m_bytes) || (f.isFloat != m_isFloat);
    m_isFloat    = f.isFloat;
    m_bytes      = f.bytes;
    m_ch         = f.ch;
    m_sr         = f.sr;
    m_frameBytes = m_bytes * m_ch;
    m_processable = true;
    if (changed) {
        RebuildCores();
        ApplySettings();            // m_ch 变了 -> 重算驱动上限, 并且把参数写进新 core
    }
    return true;
}

HRESULT BassMaxFilter::CheckInputType(const CMediaType* mtIn) {
    // 刻意放宽: 能处理的就处理, 不能处理的原样透传。
    // 真正决定"接不接"的是下游渲染器 (CTransInPlaceInputPin::CheckMediaType 会转发
    // QueryAccept), 所以这里没有理由自己把格式判死。
    if (!mtIn) return E_POINTER;
    if (!mtIn->IsValid()) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtIn->Type(), MEDIATYPE_Audio)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtIn->FormatType(), FORMAT_WaveFormatEx)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!mtIn->Format() || mtIn->FormatLength() < sizeof(WAVEFORMATEX))
        return VFW_E_TYPE_NOT_ACCEPTED;
    return S_OK;
}

HRESULT BassMaxFilter::SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt) {
    HRESULT hr = CTransInPlaceFilter::SetMediaType(dir, pmt);
    if (FAILED(hr)) return hr;
    if (dir == PINDIR_INPUT && pmt && pmt->IsValid()) SetFormatFromType(pmt);
    return S_OK;
}

HRESULT BassMaxFilter::Transform(IMediaSample* pSample) {
    if (!pSample) return S_OK;

    // --- 运行中格式变更: 采样上挂着新的媒体类型 (LAV 切音轨走的就是这条路) ---
    AM_MEDIA_TYPE* pmt = nullptr;
    if (S_OK == pSample->GetMediaType(&pmt) && pmt) {
        CMediaType mt(*pmt);
        DeleteMediaType(pmt);
        SetFormatFromType(&mt);
    }

    // 改不动就不改: 原样放过。绝不能在这里返回失败代码。
    if (!m_processable || m_sr == 0 || m_ch < 1 || m_frameBytes <= 0) return S_OK;

    // 用户在属性页/托盘改了参数? 在音频线程里安全地重建系数
    if (bassmax::SettingsGen() != m_gen) ApplySettings();
    if (!m_processable) return S_OK;      // ApplySettings 里重建失败时保底

    // 上游缓冲只读就不碰 (CTIP 需要时已经先复制过了, 这里只是兜底)
    if (InputPin() && InputPin()->ReadOnly() && !UsingDifferentAllocators()) return S_OK;

    BYTE* p = nullptr;
    if (FAILED(pSample->GetPointer(&p)) || !p) return S_OK;

    long len = pSample->GetActualDataLength();
    const long cap = pSample->GetSize();
    if (len <= 0 || cap <= 0) return S_OK;
    if (len > cap) len = cap;                         // 绝不越过实际缓冲区

    const long frames = len / m_frameBytes;
    if (frames <= 0) return S_OK;

    const int pairs = (m_ch + 1) / 2;
    if ((int)m_cores.size() < pairs) {
        RebuildCores();
        ApplySettings();          // 声道数变了 -> 驱动上限要重算
    }
    if ((int)m_cores.size() < pairs || !m_processable) return S_OK;

    // 快路: float32 立体声直接原地算, 省一次 gather/scatter
    if (m_isFloat && m_bytes == 4 && m_ch == 2 &&
        (((uintptr_t)p & 3u) == 0)) {
        m_cores[0].process(reinterpret_cast<float*>(p), (int)frames, (float)m_sr);
        return S_OK;
    }

    if (!ReserveScratch((size_t)frames)) return S_OK;  // 分配失败: 透传

    BassMaxFmt f;
    f.isFloat = m_isFloat; f.bytes = m_bytes; f.ch = m_ch; f.sr = m_sr; f.ok = true;

    float* st = m_scratch.data();
    for (int k = 0; k < pairs; ++k) {
        const int c0 = 2 * k;
        const int c1 = (c0 + 1 < m_ch) ? (c0 + 1) : c0;   // 奇数声道时最后一对复制同一路
        const BYTE* s0 = p + (size_t)c0 * m_bytes;
        const BYTE* s1 = p + (size_t)c1 * m_bytes;
        for (long i = 0; i < frames; ++i) {
            st[2*i]     = ReadSample(s0 + (size_t)i * m_frameBytes, f);
            st[2*i + 1] = ReadSample(s1 + (size_t)i * m_frameBytes, f);
        }

        m_cores[k].process(st, (int)frames, (float)m_sr);

        BYTE* d0 = p + (size_t)c0 * m_bytes;
        BYTE* d1 = p + (size_t)c1 * m_bytes;
        for (long i = 0; i < frames; ++i) {
            WriteSample(d0 + (size_t)i * m_frameBytes, f, st[2*i]);
            if (c1 != c0) WriteSample(d1 + (size_t)i * m_frameBytes, f, st[2*i + 1]);
        }
    }
    return S_OK;
}

// ---------- 属性页 ----------
STDMETHODIMP BassMaxFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (IsEqualGUID(riid, IID_ISpecifyPropertyPages))
        return GetInterface((ISpecifyPropertyPages*)this, ppv);
    return CTransInPlaceFilter::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP BassMaxFilter::GetPages(CAUUID* pPages) {
    if (!pPages) return E_POINTER;
    pPages->pElems = (GUID*)CoTaskMemAlloc(sizeof(GUID));
    if (!pPages->pElems) { pPages->cElems = 0; return E_OUTOFMEMORY; }
    pPages->cElems = 1;
    pPages->pElems[0] = CLSID_BassMaxPage;
    return S_OK;
}
