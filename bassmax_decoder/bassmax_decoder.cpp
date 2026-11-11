// bassmax_decoder.cpp — BassMax "解码器角色" 壳 (方案 B)
//
// MPC-BE 1.9.3 的音频链里, 外部 transform 滤镜永远抢不过内置 Audio Switcher
// (MERIT64_HIGHEST = 1<<58, 外部滤镜上限 1<<48)。但它允许外部滤镜顶替
// "解码器" 角色 —— 用户实测 LAV Audio Decoder 就是这样替换掉 MPC Audio Decoder 的。
//
// 所以本滤镜对外伪装成一个音频解码器:
//     输入 = 压缩音频 (AAC / MP3 / AC3 / ...)
//     输出 = PCM
// 内部用私有 IGraphBuilder 挂一个真正的 LAV Audio Decoder 负责解码,
// 解码出来的 PCM 在推给下游之前地做 BassMax DSP。
//
//                    MPC-BE 的 graph
//   [Source] --(压缩)--> [BmdFilter.In]        [BmdFilter.Out] --(PCM)--> [Audio Switcher] --> [Renderer]
//                             |                        ^
//                             | IMemInputPin 转发        | Deliver
//                             v                        |
//                        私有 graph: [Feeder.Out] -> [LAV Audio Decoder] -> [Sink.In]
//
// 关键点:
//   * Feeder 只是给 LAV 输入引脚一个合法的"已连接"状态, 真正的数据由 In 引脚
//     直接把上游的 IMediaSample 交给 LAV 的 IMemInputPin::Receive (零拷贝)。
//   * Sink 收到解码后的 PCM, 用 Out 引脚的 delivery allocator 取一块缓冲,
//     复制 + DSP 处理后 Deliver 给下游。
//   * Run/Pause/Stop 与 Flush/EOS/NewSegment 全部转发到私有 graph 和下游,
//     保证 seeking / 切音轨 / 流结束不会丢事件。
//   * 对外只暴露一个 filter, 私有 graph 里的三个 filter 不会污染 MPC-BE 的
//     "Try filters in the graph" 阶段。
#include <windows.h>
#include <dshow.h>
#include <streams.h>
#include <stdio.h>
#include <stdarg.h>
#include <new>
#include "bassmax_core.hpp"
#include "bassmax_dsp.h"
#include "bassmax_settings.h"
#include "bassmax_decoder_tray.h"

// 本滤镜 CLSID (与 bassmax_dshow.ax 的 C9B1/C9B2/C9B3 同族, 用 C9B4)
static const GUID CLSID_BassMaxDecoder = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb4}
};
// LAV Audio Decoder (LAVFilters 官方 CLSID)
static const GUID CLSID_LAVAudio = {
    0xE8E73B6B, 0x4CB3, 0x44A4, {0xBE,0x99,0x4F,0x7B,0xCB,0x96,0xE4,0x91}
};
static const GUID CAT_Legacy = {
    0x083863F1, 0x70DE, 0x11d0, {0xBD,0x40,0x00,0xA0,0xC9,0x11,0xCE,0x86}
};

// ---------------- 诊断日志 ----------------
static void BLog(const char* fmt, ...) {
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "bassmax_decoder.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    fprintf(f, "%8lu [%5lu] ", (unsigned long)GetTickCount(), (unsigned long)GetCurrentThreadId());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}
static void BLogMt(const char* tag, const CMediaType* mt) {
    if (!mt) { BLog("  %s <null>", tag); return; }
    const WAVEFORMATEX* w = (mt->cbFormat >= sizeof(WAVEFORMATEX) && mt->pbFormat)
                          ? (const WAVEFORMATEX*)mt->pbFormat : nullptr;
    BLog("  %s major=%08lX sub=%08lX fmt=%08lX len=%lu tag=%u ch=%u sr=%lu bits=%u align=%u",
         tag, (unsigned long)mt->majortype.Data1, (unsigned long)mt->subtype.Data1,
         (unsigned long)mt->formattype.Data1, (unsigned long)mt->cbFormat,
         w ? (unsigned)w->wFormatTag : 0, w ? (unsigned)w->nChannels : 0,
         w ? (unsigned long)w->nSamplesPerSec : 0UL,
         w ? (unsigned)w->wBitsPerSample : 0, w ? (unsigned)w->nBlockAlign : 0);
}

class BmdFilter;

// ---------------- 私有 graph: 假源 ----------------
class BmdFeederOut : public CBaseOutputPin {
public:
    BmdFilter* m_owner;
    BmdFeederOut(BmdFilter* owner, CBaseFilter* f, CCritSec* lk, HRESULT* phr)
        : CBaseOutputPin(NAME("BMD feeder out"), f, lk, phr, L"Out"), m_owner(owner) {}
    HRESULT CheckMediaType(const CMediaType*) override { return S_OK; }
    HRESULT GetMediaType(int i, CMediaType* pmt) override;
    HRESULT DecideBufferSize(IMemAllocator* p, ALLOCATOR_PROPERTIES* pr) override {
        pr->cBuffers = 4; pr->cbBuffer = 65536; pr->cbAlign = 1; pr->cbPrefix = 0;
        ALLOCATOR_PROPERTIES act;
        return p->SetProperties(pr, &act);
    }
};

class BmdFeeder : public CBaseFilter {
public:
    CCritSec m_cs;
    BmdFeederOut* m_out;
    BmdFeeder(BmdFilter* owner, HRESULT* phr)
        : CBaseFilter(NAME("BMD feeder"), nullptr, &m_cs, GUID_NULL, phr), m_out(nullptr) {
        m_out = new BmdFeederOut(owner, this, &m_cs, phr);
    }
    ~BmdFeeder() { delete m_out; }
    int GetPinCount() override { return 1; }
    CBasePin* GetPin(int n) override { return n == 0 ? m_out : nullptr; }
};

// ---------------- 私有 graph: PCM 接收 ----------------
class BmdSinkIn : public CBaseInputPin {
public:
    BmdFilter* m_owner;
    BmdSinkIn(BmdFilter* owner, CBaseFilter* f, CCritSec* lk, HRESULT* phr)
        : CBaseInputPin(NAME("BMD sink in"), f, lk, phr, L"In"), m_owner(owner) {}
    HRESULT CheckMediaType(const CMediaType* mt) override {
        if (!mt) return E_POINTER;
        if (*mt->Type() != MEDIATYPE_Audio || *mt->FormatType() != FORMAT_WaveFormatEx)
            return VFW_E_TYPE_NOT_ACCEPTED;
        return S_OK;
    }
    STDMETHODIMP Receive(IMediaSample* p) override;
    STDMETHODIMP EndOfStream(void) override;
    STDMETHODIMP BeginFlush(void) override;
    STDMETHODIMP EndFlush(void) override;
    STDMETHODIMP NewSegment(REFERENCE_TIME a, REFERENCE_TIME b, double r) override;
};

class BmdSink : public CBaseFilter {
public:
    CCritSec m_cs;
    BmdSinkIn* m_in;
    BmdSink(BmdFilter* owner, HRESULT* phr)
        : CBaseFilter(NAME("BMD sink"), nullptr, &m_cs, GUID_NULL, phr), m_in(nullptr) {
        m_in = new BmdSinkIn(owner, this, &m_cs, phr);
    }
    ~BmdSink() { delete m_in; }
    int GetPinCount() override { return 1; }
    CBasePin* GetPin(int n) override { return n == 0 ? m_in : nullptr; }
};

// ---------------- 对外输出引脚 (PCM) ----------------
class BmdOutPin : public CBaseOutputPin {
public:
    BmdFilter* m_owner;
    BmdOutPin(BmdFilter* owner, CBaseFilter* f, CCritSec* lk, HRESULT* phr)
        : CBaseOutputPin(NAME("BMD output pin"), f, lk, phr, L"Out"), m_owner(owner) {}
    HRESULT CheckMediaType(const CMediaType* mt) override {
        if (!mt) return E_POINTER;
        return (*mt->Type() == MEDIATYPE_Audio && *mt->FormatType() == FORMAT_WaveFormatEx)
               ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
    }
    HRESULT GetMediaType(int i, CMediaType* pmt) override;
    HRESULT DecideBufferSize(IMemAllocator* p, ALLOCATOR_PROPERTIES* pr) override;
    HRESULT CompleteConnect(IPin* pReceivePin) override {
        BLog("OutPin::CompleteConnect");
        BLogMt("  out connected", &m_mt);
        return CBaseOutputPin::CompleteConnect(pReceivePin);
    }
    const CMediaType& ConnectedMt() const { return m_mt; }
};

// ---------------- 对外输入引脚 (压缩音频, 代理 LAV 输入) ----------------
class BmdInPin : public CBaseInputPin {
public:
    BmdFilter* m_owner;
    BmdInPin(BmdFilter* owner, CBaseFilter* f, CCritSec* lk, HRESULT* phr)
        : CBaseInputPin(NAME("BMD input pin"), f, lk, phr, L"In"), m_owner(owner) {}
    HRESULT CheckMediaType(const CMediaType* mt) override;   // 实现见下方 (需要完整的 BmdFilter)
    HRESULT SetMediaType(const CMediaType* pmt) override;
    HRESULT CompleteConnect(IPin* pReceivePin) override;
    HRESULT BreakConnect() override;
    STDMETHODIMP Receive(IMediaSample* p) override;
    STDMETHODIMP EndOfStream(void) override;
    STDMETHODIMP BeginFlush(void) override;
    STDMETHODIMP EndFlush(void) override;
    STDMETHODIMP NewSegment(REFERENCE_TIME a, REFERENCE_TIME b, double r) override;
};

// ---------------- 对外滤镜 ----------------
class BmdFilter : public CBaseFilter {
public:
    CCritSec   m_cs;
    BmdInPin*  m_in  = nullptr;
    BmdOutPin* m_out = nullptr;

    IGraphBuilder* m_graph = nullptr;
    BmdFeeder*     m_feeder = nullptr;   // 由 m_graph 持有引用
    BmdSink*       m_sink   = nullptr;   // 由 m_graph 持有引用
    IBaseFilter*   m_lav    = nullptr;
    IPin*          m_lavIn  = nullptr;
    IPin*          m_lavOut = nullptr;
    IMemInputPin*  m_lavInMem = nullptr;

    CMediaType m_inMt;
    CMediaType m_pcmMt;
    bool       m_havePcm = false;
    BassMaxDsp m_dsp;
    bool       m_dspReady = false;
    bool       m_privateBuilt = false;
    DWORD      m_lastReload = 0;   // 节流: 每 400ms 从注册表同步一次参数
    LONG       m_statSamples = 0;  // 诊断: 累计进入 DeliverPcm 的样本数
    LONGLONG   m_statBytes = 0;    // 诊断: 累计交给 DSP 的字节数
    LONG       m_statChanged = 0;  // 诊断: 数据确实被 DSP 改动的样本数 (bypass 时恒 0)
    DWORD      m_lastStat = 0;     // 诊断: 上次打点时间
    bool       m_mtDirty = false;  // 私有图重建后 PCM 格式变了 -> 下一块 sample 带新格式

    BmdFilter(LPUNKNOWN pUnk, HRESULT* phr);
    ~BmdFilter();

    int GetPinCount() override { return 2; }
    CBasePin* GetPin(int n) override { return n == 0 ? (CBasePin*)m_in : n == 1 ? (CBasePin*)m_out : nullptr; }
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;
    STDMETHODIMP Stop() override;

    const CMediaType& InputMt() const { return m_inMt; }
    HRESULT PcmMt(CMediaType* pmt);
    void    SetInputMt(const CMediaType* pmt);
    HRESULT BuildPrivateGraph();
    HRESULT DeliverPcm(IMediaSample* pIn);
    void    PrepareDsp(const CMediaType* mt);

    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr);
};

// ---------------- 引脚实现 ----------------
HRESULT BmdFeederOut::GetMediaType(int i, CMediaType* pmt) {
    if (!pmt) return E_POINTER;
    if (i != 0) return VFW_S_NO_MORE_ITEMS;
    *pmt = m_owner->InputMt();
    BLogMt("feeder offer", pmt);
    return S_OK;
}

HRESULT BmdOutPin::GetMediaType(int i, CMediaType* pmt) {
    if (!pmt) return E_POINTER;
    if (i != 0) return VFW_S_NO_MORE_ITEMS;
    HRESULT hr = m_owner->PcmMt(pmt);
    BLog("OutPin::GetMediaType(%d) -> 0x%08lX", i, (unsigned long)hr);
    if (SUCCEEDED(hr)) BLogMt("  out offer", pmt);
    return hr;
}

HRESULT BmdOutPin::DecideBufferSize(IMemAllocator* p, ALLOCATOR_PROPERTIES* pr) {
    CMediaType mt;
    long need = 65536;
    if (SUCCEEDED(m_owner->PcmMt(&mt)) && mt.cbFormat >= sizeof(WAVEFORMATEX) && mt.pbFormat) {
        const WAVEFORMATEX* w = (const WAVEFORMATEX*)mt.pbFormat;
        if (w->nAvgBytesPerSec > 0) {
            long long n = (long long)w->nAvgBytesPerSec / 4 + 16384;   // ~0.25s
            if (n < 65536) n = 65536;
            if (n > 4 * 1024 * 1024) n = 4 * 1024 * 1024;
            need = (long)n;
        }
    }
    pr->cBuffers = 4; pr->cbBuffer = need; pr->cbAlign = 1; pr->cbPrefix = 0;
    ALLOCATOR_PROPERTIES act;
    HRESULT hr = p->SetProperties(pr, &act);
    BLog("OutPin::DecideBufferSize need=%ld -> 0x%08lX", need, (unsigned long)hr);
    if (FAILED(hr)) return hr;
    return (act.cbBuffer < need) ? E_FAIL : S_OK;
}

HRESULT BmdInPin::CheckMediaType(const CMediaType* mt) {
    if (!mt) return E_POINTER;
    if (*mt->Type() != MEDIATYPE_Audio) return VFW_E_TYPE_NOT_ACCEPTED;
    // 只当"解码器"用: 拒绝已经解码的 PCM, 免得被图管理器当成 PCM→PCM
    // 的普通 transform 去和内置 Audio Switcher 抢位置。
    if (*mt->Subtype() == MEDIASUBTYPE_PCM ||
        *mt->Subtype() == MEDIASUBTYPE_IEEE_FLOAT)
        return VFW_E_TYPE_NOT_ACCEPTED;

    // 关键回退机制: MPC-BE 里本滤镜被配成 PREFERRED + Audio/NULL 通配,
    // 所有音频流都会先塞给我们。如果等到 CompleteConnect 里私有 LAV 图
    // 才报"类型不接受", 整个 graph 直接失败并弹"无法渲染文件", 不会
    // 回退到内置解码器 (APE 单文件打开就是这个下场)。
    // 所以这里先问内部 LAV 输入引脚认不认这个压缩类型, 不认就当场拒绝,
    // 图管理器自然会继续尝试下一个候选滤镜。
    // 只对明确的"不接受"回退; 其它错误码放行, 防止 LAV 在未连接状态下
    // QueryAccept 不可用而把本来能解的格式也误拒。
    IPin* lavIn = m_owner ? m_owner->m_lavIn : nullptr;
    if (lavIn) {
        HRESULT hr = lavIn->QueryAccept(static_cast<const AM_MEDIA_TYPE*>(mt));
        BLog("InPin::CheckMediaType: sub=%08lX tag=%u LAV QueryAccept -> 0x%08lX",
             (unsigned long)mt->subtype.Data1,
             (mt->cbFormat >= sizeof(WAVEFORMATEX) && mt->pbFormat)
                 ? (unsigned)((const WAVEFORMATEX*)mt->pbFormat)->wFormatTag : 0,
             (unsigned long)hr);
        if (hr == S_FALSE || hr == VFW_E_TYPE_NOT_ACCEPTED) {
            BLog("  -> 拒绝, 交回内置解码器 (避免整张图报无法渲染文件)");
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
    }
    return S_OK;
}

HRESULT BmdInPin::SetMediaType(const CMediaType* pmt) {
    BLogMt("InPin::SetMediaType", pmt);
    HRESULT hr = CBaseInputPin::SetMediaType(pmt);
    if (SUCCEEDED(hr)) m_owner->SetInputMt(pmt);
    return hr;
}

HRESULT BmdInPin::CompleteConnect(IPin* pReceivePin) {
    BLog("InPin::CompleteConnect");
    HRESULT hr = CBaseInputPin::CompleteConnect(pReceivePin);
    if (SUCCEEDED(hr)) hr = m_owner->BuildPrivateGraph();
    BLog("InPin::CompleteConnect -> 0x%08lX", (unsigned long)hr);
    return hr;
}

HRESULT BmdInPin::BreakConnect() {
    BLog("InPin::BreakConnect");
    return CBaseInputPin::BreakConnect();
}

STDMETHODIMP BmdInPin::Receive(IMediaSample* p) {
    if (!p) return E_POINTER;
    if (!m_owner->m_lavInMem) return VFW_E_NOT_CONNECTED;
    return m_owner->m_lavInMem->Receive(p);   // 零拷贝转发给 LAV 输入
}

STDMETHODIMP BmdInPin::EndOfStream(void) {
    BLog("InPin::EndOfStream");
    if (m_owner->m_lavIn) m_owner->m_lavIn->EndOfStream();
    return S_OK;
}

STDMETHODIMP BmdInPin::BeginFlush(void) {
    BLog("InPin::BeginFlush");
    if (m_owner->m_lavIn) m_owner->m_lavIn->BeginFlush();
    return S_OK;
}

STDMETHODIMP BmdInPin::EndFlush(void) {
    BLog("InPin::EndFlush");
    if (m_owner->m_lavIn) m_owner->m_lavIn->EndFlush();
    return S_OK;
}

STDMETHODIMP BmdInPin::NewSegment(REFERENCE_TIME a, REFERENCE_TIME b, double r) {
    BLog("InPin::NewSegment");
    if (m_owner->m_lavIn) m_owner->m_lavIn->NewSegment(a, b, r);
    return S_OK;
}

STDMETHODIMP BmdSinkIn::Receive(IMediaSample* p) {
    if (!p) return E_POINTER;
    // 切音轨 / 源切换时 LAV 会通过 sample 的 media type 动态告知新格式。
    // 我们覆盖了 CBaseInputPin::Receive, 基类里"收到新格式就更新 m_mt"的
    // 逻辑不会自动跑, 必须自己处理, 否则 DSP 还按旧声道数/采样率算。
    AM_MEDIA_TYPE* pmt = nullptr;
    if (SUCCEEDED(p->GetMediaType(&pmt)) && pmt && pmt->cbFormat) {
        CMediaType mt(*pmt);
        BLogMt("SinkIn::Receive 动态格式变化", &mt);
        SetMediaType(&mt);
        m_owner->PrepareDsp(&mt);
        DeleteMediaType(pmt);
    }
    if (!m_owner->m_dspReady) m_owner->PrepareDsp(&m_mt);
    return m_owner->DeliverPcm(p);
}

STDMETHODIMP BmdSinkIn::EndOfStream(void) {
    BLog("SinkIn::EndOfStream STAT-FINAL samples=%ld bytes=%lld changed=%ld",
         (long)m_owner->m_statSamples, (long long)m_owner->m_statBytes,
         (long)m_owner->m_statChanged);
    return m_owner->m_out ? m_owner->m_out->DeliverEndOfStream() : S_OK;
}

STDMETHODIMP BmdSinkIn::BeginFlush(void) {
    BLog("SinkIn::BeginFlush");
    return m_owner->m_out ? m_owner->m_out->DeliverBeginFlush() : S_OK;
}

STDMETHODIMP BmdSinkIn::EndFlush(void) {
    BLog("SinkIn::EndFlush");
    return m_owner->m_out ? m_owner->m_out->DeliverEndFlush() : S_OK;
}

STDMETHODIMP BmdSinkIn::NewSegment(REFERENCE_TIME a, REFERENCE_TIME b, double r) {
    BLog("SinkIn::NewSegment");
    return m_owner->m_out ? m_owner->m_out->DeliverNewSegment(a, b, r) : S_OK;
}

// ---------------- 滤镜实现 ----------------
BmdFilter::BmdFilter(LPUNKNOWN pUnk, HRESULT* phr)
    : CBaseFilter(NAME("BassMax Audio Decoder (LAV Wrapper)"), pUnk, &m_cs, CLSID_BassMaxDecoder) {
    BLog("=== CREATE BmdFilter ===");
    bassmax::InitSettings();
    // 进程内托盘: 播放时图标出现, 单击即可切总开关。不注册 COM 类,
    // 与属性页 (ISpecifyPropertyPages / CLSID ...C9B2) 完全无关。
    bassmax::DecoderTrayAddRef();
    HRESULT hr = S_OK;
    m_in  = new BmdInPin(this, this, &m_cs, &hr);
    m_out = new BmdOutPin(this, this, &m_cs, &hr);

    do {
        hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IGraphBuilder, (void**)&m_graph);
        if (FAILED(hr)) { BLog("FilterGraph 创建失败 0x%08lX", (unsigned long)hr); break; }

        // 私有 graph 不要参考时钟: 真正的时钟在 MPC-BE 的图里 (渲染器)。
        IMediaFilter* mf = nullptr;
        if (SUCCEEDED(m_graph->QueryInterface(IID_IMediaFilter, (void**)&mf)) && mf) {
            mf->SetSyncSource(nullptr);
            mf->Release();
        }

        hr = CoCreateInstance(CLSID_LAVAudio, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IBaseFilter, (void**)&m_lav);
        if (FAILED(hr)) { BLog("LAV Audio 创建失败 0x%08lX (LAVAudio.ax 未注册?)", (unsigned long)hr); break; }
        BLog("LAV Audio 已创建");

        hr = m_graph->AddFilter(m_lav, L"LAV Audio Decoder (internal)");
        if (FAILED(hr)) { BLog("AddFilter(LAV) 失败 0x%08lX", (unsigned long)hr); break; }

        m_feeder = new BmdFeeder(this, &hr);
        hr = m_graph->AddFilter(m_feeder, L"BMD Feeder");
        if (FAILED(hr)) { BLog("AddFilter(Feeder) 失败 0x%08lX", (unsigned long)hr); break; }

        m_sink = new BmdSink(this, &hr);
        hr = m_graph->AddFilter(m_sink, L"BMD Sink");
        if (FAILED(hr)) { BLog("AddFilter(Sink) 失败 0x%08lX", (unsigned long)hr); break; }

        // 找 LAV 的输入 / 输出引脚
        IEnumPins* ep = nullptr;
        if (SUCCEEDED(m_lav->EnumPins(&ep)) && ep) {
            IPin* p = nullptr;
            while (ep->Next(1, &p, nullptr) == S_OK && p) {
                PIN_DIRECTION d;
                if (SUCCEEDED(p->QueryDirection(&d))) {
                    if (d == PINDIR_INPUT && !m_lavIn)  { m_lavIn = p; p = nullptr; }
                    if (d == PINDIR_OUTPUT && !m_lavOut) { m_lavOut = p; p = nullptr; }
                }
                if (p) { p->Release(); p = nullptr; }
            }
            ep->Release();
        }
        if (!m_lavIn || !m_lavOut) { BLog("LAV 引脚枚举失败 in=%p out=%p", m_lavIn, m_lavOut); hr = E_FAIL; break; }
        hr = m_lavIn->QueryInterface(IID_IMemInputPin, (void**)&m_lavInMem);
        if (FAILED(hr)) { BLog("LAV 输入无 IMemInputPin 0x%08lX", (unsigned long)hr); break; }
        BLog("私有 graph 就绪");
    } while (0);

    if (phr) *phr = hr;
}

BmdFilter::~BmdFilter() {
    BLog("=== DESTROY BmdFilter === samples=%ld bytes=%lld changed=%ld enabled=%d",
         (long)m_statSamples, (long long)m_statBytes, (long)m_statChanged,
         (int)bassmax::GetSettings().enabled);
    // 只发一条异步消息隐藏图标, 不等待托盘线程 —— 析构可能跑在拆图线程上
    bassmax::DecoderTrayRelease();
    if (m_lavInMem) { m_lavInMem->Release(); m_lavInMem = nullptr; }
    if (m_lavIn)    { m_lavIn->Release();    m_lavIn = nullptr; }
    if (m_lavOut)   { m_lavOut->Release();   m_lavOut = nullptr; }
    // feeder/sink/lav 由 m_graph 持有引用, Release graph 时会一起释放
    if (m_lav)   { m_lav->Release();   m_lav = nullptr; }
    if (m_graph) { m_graph->Release(); m_graph = nullptr; }
    delete m_in;  m_in = nullptr;
    delete m_out; m_out = nullptr;
}

STDMETHODIMP BmdFilter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) {
    BLog("JoinFilterGraph graph=%p name=%ls", pGraph, pName ? pName : L"(null)");
    return CBaseFilter::JoinFilterGraph(pGraph, pName);
}

static void GraphState(IGraphBuilder* g, const char* tag, int state) {
    if (!g) return;
    IMediaControl* mc = nullptr;
    if (SUCCEEDED(g->QueryInterface(IID_IMediaControl, (void**)&mc)) && mc) {
        HRESULT hr = state == 0 ? mc->Stop() : state == 1 ? mc->Pause() : mc->Run();
        BLog("私有 graph %s -> 0x%08lX", tag, (unsigned long)hr);
        mc->Release();
    }
}
STDMETHODIMP BmdFilter::Stop()  {
    BLog("BmdFilter::Stop STAT-FINAL samples=%ld bytes=%lld changed=%ld",
         (long)m_statSamples, (long long)m_statBytes, (long)m_statChanged);
    HRESULT hr = CBaseFilter::Stop();  GraphState(m_graph, "Stop",  0); return hr;
}
STDMETHODIMP BmdFilter::Pause() { HRESULT hr = CBaseFilter::Pause(); GraphState(m_graph, "Pause", 1); return hr; }
STDMETHODIMP BmdFilter::Run(REFERENCE_TIME t) { HRESULT hr = CBaseFilter::Run(t); GraphState(m_graph, "Run", 2); return hr; }

void BmdFilter::SetInputMt(const CMediaType* pmt) {
    if (pmt) m_inMt = *pmt;
}

HRESULT BmdFilter::PcmMt(CMediaType* pmt) {
    if (!pmt) return E_POINTER;
    // 有些图管理器会先枚举输出引脚的媒体类型、再连接输入引脚。此时 m_inMt
    // 已经有了(SetMediaType 已调用), 可以抢先建好私有 graph, 拿到 LAV 的
    // 真实 PCM 格式再应答, 避免输出侧拿到错误的占位格式。
    if (!m_havePcm && m_in && m_in->IsConnected() && !m_privateBuilt) {
        BLog("PcmMt: 输出协商早于 BuildPrivateGraph, 提前构建");
        BuildPrivateGraph();
    }
    if (!m_havePcm) {
        // 输出协商早于输入连接 (或私有图还没建好): 绝对不能返回失败。
        // GetMediaType 返回失败会让 MPC-BE 在枚举阶段直接丢弃本滤镜,
        // PREFERRED 又没有后备 -> 弹"无法渲染文件"。
        // 先给一个合法的占位 PCM 类型, 等输入连上拿到 LAV 的真实格式后,
        // 由 m_mtDirty 在下一块 sample 上挂动态媒体类型通知下游。
        int ch = 2; DWORD sr = 44100;
        if (m_inMt.cbFormat >= sizeof(WAVEFORMATEX) && m_inMt.pbFormat) {
            const WAVEFORMATEX* w = (const WAVEFORMATEX*)m_inMt.pbFormat;
            if (w->nChannels > 0 && w->nChannels <= 8) ch = w->nChannels;
            if (w->nSamplesPerSec >= 8000 && w->nSamplesPerSec <= 384000) sr = w->nSamplesPerSec;
        }
        WAVEFORMATEX wf = {};
        wf.wFormatTag       = WAVE_FORMAT_PCM;
        wf.nChannels        = (WORD)ch;
        wf.nSamplesPerSec   = sr;
        wf.wBitsPerSample   = 16;
        wf.nBlockAlign      = (WORD)(ch * 2);
        wf.nAvgBytesPerSec  = sr * (DWORD)wf.nBlockAlign;
        wf.cbSize           = 0;
        CMediaType mt;
        mt.SetType(&MEDIATYPE_Audio);
        mt.SetSubtype(&MEDIASUBTYPE_PCM);
        mt.SetFormatType(&FORMAT_WaveFormatEx);
        mt.SetFormat((BYTE*)&wf, sizeof(wf));
        *pmt = mt;
        BLog("PcmMt: LAV 未就绪, 返回占位 PCM ch=%d sr=%lu (不再返回 NOT_CONNECTED)",
             ch, (unsigned long)sr);
        return S_OK;
    }
    *pmt = m_pcmMt;
    return S_OK;
}

HRESULT BmdFilter::BuildPrivateGraph() {
    if (!m_graph || !m_feeder || !m_lavIn || !m_lavOut || !m_sink) {
        BLog("BuildPrivateGraph: 私有 graph 未就绪");
        return E_UNEXPECTED;
    }
    BLog("BuildPrivateGraph: 输入类型:");
    BLogMt("  in", &m_inMt);

    // 切音轨 / 重连时先停私有图再拆旧连接。LAV 有解码工作线程,
    // 在 Run 状态直接 Disconnect 会失败甚至挂住。
    if (m_privateBuilt) {
        GraphState(m_graph, "Stop(rebuild)", 0);
        m_graph->Disconnect(m_lavOut);
        m_graph->Disconnect(m_sink->m_in);
        m_graph->Disconnect(m_lavIn);
        m_graph->Disconnect(m_feeder->m_out);
        m_privateBuilt = false;
        m_havePcm = false;
    }

    // 1) Feeder.Out -> LAV.In, 用上游真实压缩类型直连
    HRESULT hr = m_graph->ConnectDirect(m_feeder->m_out, m_lavIn, &m_inMt);
    BLog("  ConnectDirect(Feeder->LAV) -> 0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) {
        // 有些格式 (APE 等) 源给的类型块带额外字段, LAV 会拒。清掉半连接
        // 状态后再试一次"不指定类型"的自动协商, 让双方自己枚举。
        m_graph->Disconnect(m_feeder->m_out);
        m_graph->Disconnect(m_lavIn);
        hr = m_graph->ConnectDirect(m_feeder->m_out, m_lavIn, nullptr);
        BLog("  ConnectDirect(Feeder->LAV, 自动协商) -> 0x%08lX", (unsigned long)hr);
    }
    if (FAILED(hr)) return hr;

    // 2) LAV.Out -> Sink.In, 自动协商 PCM 类型
    IPin* sinkIn = m_sink->m_in;
    hr = m_graph->ConnectDirect(m_lavOut, sinkIn, nullptr);
    BLog("  ConnectDirect(LAV->Sink) -> 0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) return hr;

    AM_MEDIA_TYPE mt = {};
    if (SUCCEEDED(m_lavOut->ConnectionMediaType(&mt))) {
        m_pcmMt.Set(mt);
        m_havePcm = true;
        BLogMt("  LAV 输出 PCM", &m_pcmMt);
        FreeMediaType(mt);
    }
    // 切音轨时输出侧可能已经按旧格式连着下游。此时不能硬 Reconnect(会和图
    // 管理器的连接过程互相抢锁), 改为在下一个输出 sample 上挂新格式, 让
    // 下游按 DirectShow 动态媒体类型规则更新。
    if (m_out && m_out->IsConnected() && m_havePcm &&
        !(m_pcmMt == m_out->ConnectedMt())) {
        m_mtDirty = true;
        BLog("  PCM 格式与已连接输出不一致 -> 下一块 sample 附新格式");
        BLogMt("  out was", &m_out->ConnectedMt());
    }
    m_privateBuilt = true;
    m_dspReady = false;
    return S_OK;
}

void BmdFilter::PrepareDsp(const CMediaType* mt) {
    BLogMt("PrepareDsp", mt);
    m_dsp.SetFormat(mt);
    m_dspReady = true;
    // 把总开关状态写进日志: 之前只统计"调过 Process", 但总开关关闭时
    // Core 内部是 bypass, 数据一个字节都不会变, 光看 samples/bytes 会误判。
    bassmax::Settings s = bassmax::GetSettings();
    BLog("  DSP 总开关 enabled=%d bass=%.2f treble=%.2f drive=%.2f preset=%d",
         (int)s.enabled, s.bass, s.treble, s.drive, s.preset);
    BLog("  DSP processable=%d ch=%d sr=%lu frameBytes=%d",
         (int)m_dsp.Processable(), m_dsp.Channels(),
         (unsigned long)m_dsp.SampleRate(), m_dsp.FrameBytes());
}

HRESULT BmdFilter::DeliverPcm(IMediaSample* pIn) {
    // 诊断打点: 每 2 秒报一次累计样本/字节数。日志里能看到 DSP 是真的处理了
    // PCM, 而不只是"滤镜出现在列表里 / 托盘图标亮了"。
    InterlockedIncrement(&m_statSamples);
    DWORD now0 = GetTickCount();
    if ((DWORD)(now0 - m_lastStat) >= 2000) {
        m_lastStat = now0;
        BLog("STAT samples=%ld bytes=%lld changed=%ld processable=%d enabled=%d",
             (long)m_statSamples, (long long)m_statBytes, (long)m_statChanged,
             (int)m_dsp.Processable(), (int)bassmax::GetSettings().enabled);
    }
    if (!m_out || !m_out->IsConnected()) return S_OK;
    IMediaSample* pOut = nullptr;
    HRESULT hr = m_out->GetDeliveryBuffer(&pOut, nullptr, nullptr, 0);
    if (FAILED(hr) || !pOut) { BLog("GetDeliveryBuffer -> 0x%08lX", (unsigned long)hr); return hr; }

    BYTE *src = nullptr, *dst = nullptr;
    if (SUCCEEDED(pIn->GetPointer(&src)) && SUCCEEDED(pOut->GetPointer(&dst)) && src && dst) {
        long len = pIn->GetActualDataLength();
        long cap = pOut->GetSize();
        if (len > cap) len = cap;
        int fb = m_dsp.FrameBytes();
        if (fb > 0) len -= (long)(((unsigned long)len) % (unsigned long)fb);
        if (len > 0) {
            memcpy(dst, src, len);
            if (m_dsp.Processable()) {
                // 参数可能由另一个 DLL (bassmax_dshow.ax 的属性页 / 托盘 / 设置程序)
                // 写进注册表 —— 本 DLL 的 s_gen 不会跟着变, 所以按 400ms 节流
                // 主动回读一次, 再让 Tick() 判断是否重建系数。
                DWORD now = GetTickCount();
                if ((DWORD)(now - m_lastReload) >= 400) {
                    m_lastReload = now;
                    bassmax::ReloadFromRegistry();
                }
                m_dsp.Tick();
                if (m_dsp.Process(dst, len)) {
                    InterlockedAdd64(&m_statBytes, (LONGLONG)len);
                    // 真改了数据才算 DSP 生效: 总开关关闭时 Core 内部 bypass,
                    // dst 与 src 逐字节相同, changed 恒为 0。
                    // 之前只统计"调过 Process", 无法区分 bypass, 会误判成功。
                    long n = (len < 64) ? len : 64;
                    if (memcmp(dst, src, (size_t)n) != 0)
                        InterlockedIncrement(&m_statChanged);
                }
            }
        }
        pOut->SetActualDataLength(len);
    } else {
        pOut->SetActualDataLength(0);
    }

    REFERENCE_TIME t1 = 0, t2 = 0;
    if (SUCCEEDED(pIn->GetTime(&t1, &t2))) pOut->SetTime(&t1, &t2);
    pOut->SetSyncPoint(pIn->IsSyncPoint() == S_OK);
    pOut->SetDiscontinuity(pIn->IsDiscontinuity() == S_OK);
    pOut->SetPreroll(pIn->IsPreroll() == S_OK);
    AM_MEDIA_TYPE* pmt = nullptr;
    if (SUCCEEDED(pIn->GetMediaType(&pmt)) && pmt) {
        pOut->SetMediaType(pmt);
        DeleteMediaType(pmt);
    } else if (m_mtDirty && m_havePcm) {
        // 私有图重建导致 LAV 输出格式变化, 但下游还按旧格式连着。
        CMediaType mt = m_pcmMt;
        pOut->SetMediaType(&mt);
        BLogMt("DeliverPcm 附新格式", &mt);
    }
    m_mtDirty = false;

    hr = m_out->Deliver(pOut);
    pOut->Release();
    return hr;
}

CUnknown* WINAPI BmdFilter::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
    BmdFilter* p = new (std::nothrow) BmdFilter(pUnk, phr);
    if (!p && phr) *phr = E_OUTOFMEMORY;
    return p;
}

// ================= COM 入口 / 类厂 / 自注册 =================
static HMODULE g_hModule = nullptr;
HINSTANCE     g_hInst = nullptr;
DWORD         g_amPlatform = VER_PLATFORM_WIN32_NT;
OSVERSIONINFO g_osInfo = {};
CFactoryTemplate g_Templates[1] = {};
int           g_cTemplates = 0;

class CFactory : public IClassFactory {
    LONG  m_cRef = 1;
    CLSID m_clsid;
public:
    explicit CFactory(const CLSID& c) : m_clsid(c) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) {
        if (iid == IID_IUnknown || iid == IID_IClassFactory) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() { LONG r = InterlockedDecrement(&m_cRef); if (!r) delete this; return r; }
    STDMETHODIMP CreateInstance(LPUNKNOWN pUnk, REFIID iid, void** pp) {
        if (pUnk) return CLASS_E_NOAGGREGATION;
        *pp = nullptr;
        if (!IsEqualGUID(m_clsid, CLSID_BassMaxDecoder)) return CLASS_E_CLASSNOTAVAILABLE;
        HRESULT hr = S_OK;
        CUnknown* p = BmdFilter::CreateInstance(pUnk, &hr);
        if (!p) return hr;
        p->NonDelegatingAddRef();
        hr = p->NonDelegatingQueryInterface(iid, pp);
        p->NonDelegatingRelease();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL) { return S_OK; }
};

static HRESULT SetStr(HKEY hk, LPCWSTR name, LPCWSTR val) {
    LONG r = RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
    return (r == ERROR_SUCCESS) ? S_OK : HRESULT_FROM_WIN32((DWORD)r);
}
static HRESULT CreateKeyStr(LPCWSTR path, LPCWSTR name, LPCWSTR val) {
    HKEY hk; DWORD disp;
    LONG r = RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, &disp);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32((DWORD)r);
    HRESULT hr = SetStr(hk, name, val);
    RegCloseKey(hk);
    return hr;
}

// 输入声明为 Audio/通配, 输出声明为 PCM + IEEE_FLOAT —— 这正是"解码器"的形态。
static const REGPINTYPES g_inTypes[] = {
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_NULL },
};
static const REGPINTYPES g_outTypes[] = {
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_PCM },
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_IEEE_FLOAT },
};
static const REGFILTERPINS g_pins[] = {
    { (LPWSTR)L"In",  FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, 1, (REGPINTYPES*)g_inTypes },
    { (LPWSTR)L"Out", FALSE, TRUE,  FALSE, FALSE, &CLSID_NULL, nullptr, 2, (REGPINTYPES*)g_outTypes },
};

STDAPI DllRegisterServer() {
    HRESULT hrCo = CoInitialize(nullptr);
    HRESULT hrRet = S_OK;
    WCHAR path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(g_hModule, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) hrRet = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);

    WCHAR s[64]; StringFromGUID2(CLSID_BassMaxDecoder, s, 64);
    WCHAR key[320];
    if (SUCCEEDED(hrRet)) {
        wsprintfW(key, L"CLSID\\%s", s);
        hrRet = CreateKeyStr(key, nullptr, L"BassMax Audio Decoder (LAV Wrapper)");
    }
    if (SUCCEEDED(hrRet)) {
        wsprintfW(key, L"CLSID\\%s\\InprocServer32", s);
        hrRet = CreateKeyStr(key, nullptr, path);
        if (SUCCEEDED(hrRet)) hrRet = CreateKeyStr(key, L"ThreadingModel", L"Both");
    }
    if (SUCCEEDED(hrRet)) {
        IFilterMapper2* pF = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFilterMapper2, (void**)&pF);
        if (FAILED(hr)) hrRet = hr;
        else {
            REGFILTER2 rf2 = {};
            rf2.dwVersion = 1;
            rf2.dwMerit   = MERIT_UNLIKELY;
            rf2.cPins     = 2;
            rf2.rgPins    = g_pins;
            hrRet = pF->RegisterFilter(CLSID_BassMaxDecoder,
                                       L"BassMax Audio Decoder (LAV Wrapper)",
                                       nullptr, &CAT_Legacy, nullptr, &rf2);
            pF->Release();
        }
    }
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return hrRet;
}

STDAPI DllUnregisterServer() {
    HRESULT hrCo = CoInitialize(nullptr);
    IFilterMapper2* pF = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFilterMapper2, (void**)&pF))) {
        pF->UnregisterFilter(&CAT_Legacy, nullptr, CLSID_BassMaxDecoder);
        pF->Release();
    }
    WCHAR s[64]; StringFromGUID2(CLSID_BassMaxDecoder, s, 64);
    WCHAR key[320]; wsprintfW(key, L"CLSID\\%s", s);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key);
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!IsEqualGUID(rclsid, CLSID_BassMaxDecoder)) return CLASS_E_CLASSNOTAVAILABLE;
    if (riid != IID_IClassFactory && riid != IID_IUnknown) return E_NOINTERFACE;
    CFactory* f = new (std::nothrow) CFactory(rclsid);
    if (!f) return E_OUTOFMEMORY;
    *ppv = static_cast<IClassFactory*>(f);
    return S_OK;
}
STDAPI DllCanUnloadNow() { return S_FALSE; }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        g_hInst = hModule;
        g_osInfo.dwOSVersionInfoSize = sizeof(g_osInfo);
        if (GetVersionEx(&g_osInfo)) g_amPlatform = g_osInfo.dwPlatformId;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
