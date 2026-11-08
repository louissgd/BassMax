// bassmax_dsp.cpp  —  格式解析 + DSP 处理 (两个滤镜共用)
//
// 从 1.0.4 的 bassmax_filter.cpp 原样搬迁, 除了把成员变量搬进 BassMaxDsp,
// 处理逻辑一个字节没改 (1.0.4 的音色已经验证过, 不在这里冒险)。
#include "bassmax_dsp.h"
#include "bassmax_settings.h"
#include <mmreg.h>   // WAVEFORMATEXTENSIBLE / WAVE_FORMAT_* 常量

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

    // 位深可能为 0: WAV Parser 等分离器给出的 WAVEFORMATEX 只填 nBlockAlign,
    // wBitsPerSample 是 0。1.0.4 之前这种情况一律判死不处理 -> 滤镜明明在链路上,
    // 听感却和没装一样。所以这里先不判死, 等下面用 nBlockAlign 反推。
    if (bits > 0) {
        if (bits % 8 != 0) return f;
        f.bytes = bits / 8;
        if (f.isFloat) {
            if (f.bytes != 4 && f.bytes != 8) return f;
        } else {
            if (f.bytes != 1 && f.bytes != 2 && f.bytes != 3 && f.bytes != 4) return f;
        }
    }

    if (f.ch < 1 || f.ch > 8) return f;
    // 上限 1,536,000 Hz: 覆盖 DSD256 (5,644,800/4 = 1,411,200) 及以上余量。
    // 原上限 768,000 会把 DSD256 直接判为不支持 -> 滤镜不挂载、听感等于失效。
    if (f.sr < 8000 || f.sr > 1536000) return f;

    // 每声道每样本字节数, 两种来源:
    //   1) 位深/8 (正常情况);
    //   2) nBlockAlign / nChannels (位深缺失时反推)。
    // 两者不一致时以 nBlockAlign 为准 (24-in-32 这类"容器填充"只有它能说清),
    // 前提是落在 {1,2,3,4,8} 这种合理值上, 否则宁可透传也不乱解释数据。
    const int alignPerCh = (wf->nBlockAlign > 0 && (wf->nBlockAlign % f.ch) == 0)
                         ? (int)(wf->nBlockAlign / f.ch) : 0;
    if (f.bytes == 0) {
        f.bytes = alignPerCh;
    } else if (alignPerCh != 0 && alignPerCh != f.bytes) {
        f.bytes = alignPerCh;
    }
    if (f.bytes != 1 && f.bytes != 2 && f.bytes != 3 && f.bytes != 4 && f.bytes != 8) return f;
    if (f.isFloat && f.bytes != 4 && f.bytes != 8) return f;
    if (!f.isFloat && f.bytes == 8) return f;          // 64-bit 整数 PCM 不支持
    f.ok = true;
    return f;
}

// ---------- BassMaxDsp ----------
void BassMaxDsp::ApplyToAllCores() {
    if (m_sr == 0) return;
    for (size_t i = 0; i < m_cores.size(); ++i)
        m_cores[i].setParams(m_params, (float)m_sr);
}

void BassMaxDsp::ApplySettings() {
    bassmax::Settings s = bassmax::GetSettings();
    if (s.enabled) {
        m_params.bass   = s.bass;
        // 驱动: >2 声道时自动限制 (见 bassmax_dsp.h 的说明)。只影响本次生效值,
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

void BassMaxDsp::Tick() {
    if (bassmax::SettingsGen() != m_gen) ApplySettings();
}

bool BassMaxDsp::ReserveScratch(size_t frames) {
    size_t need = frames * 2;
    if (m_scratch.size() >= need) return true;
    try { m_scratch.resize(need); } catch (...) { return false; }
    return true;
}

void BassMaxDsp::RebuildCores() {
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

bool BassMaxDsp::SetFormat(const CMediaType* mt) {
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

bool BassMaxDsp::Process(BYTE* p, long len) {
    if (!m_processable || m_sr == 0 || m_ch < 1 || m_frameBytes <= 0) return false;
    if (!p || len <= 0) return false;

    const long frames = len / m_frameBytes;
    if (frames <= 0) return false;          // 不足一帧: 原样放过

    const int pairs = (m_ch + 1) / 2;
    if ((int)m_cores.size() < pairs) {
        RebuildCores();
        ApplySettings();          // 声道数变了 -> 驱动上限要重算
    }
    if ((int)m_cores.size() < pairs || !m_processable) return false;

    // 快路: float32 立体声直接原地算, 省一次 gather/scatter
    if (m_isFloat && m_bytes == 4 && m_ch == 2 &&
        (((uintptr_t)p & 3u) == 0)) {
        m_cores[0].process(reinterpret_cast<float*>(p), (int)frames, (float)m_sr);
        return true;
    }

    if (!ReserveScratch((size_t)frames)) return false;  // 分配失败: 透传

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
    return true;
}
