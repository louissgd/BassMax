// bassmax_xform.cpp  —  [已停用, 保留供追溯] CTransformFilter 版 BassMax
//
// 1.0.6 起不再编入 build.sh。MPC-BE 的音频链接不住任何 PCM→PCM 外部滤镜,
// 现由独立的 bassmax_decoder.ax 以"解码器角色"接管。详见 bassmax_xform.h。
//
// 与 CTIP 版的唯一区别是缓冲模型: 这里输出引脚有自己的分配器, 数据必须先从输入
// 样本拷到输出样本, 再在输出缓冲上就地处理。所有"格式协商"相关的坑见头文件说明。
//
// 硬性约定: Transform 永远返回 S_OK 或 S_FALSE, 绝不返回失败码。
// 音效滤镜让播放器的图崩掉, 比没有低音严重得多。
#include "bassmax_xform.h"
#include "bassmax_settings.h"
#include "bassmax_tray.h"
#include <mmreg.h>
#include <new>

BassMaxXformFilter::BassMaxXformFilter(LPUNKNOWN pUnk, HRESULT* phr)
    : CTransformFilter(NAME("BassMax Audio Enhancer (MPC-BE)"), pUnk, CLSID_BassMaxXform) {
    bassmax::InitSettings();
    m_dsp.ApplySettings();
    // 托盘线程按引用计数复用: 两个滤镜同时存在时也只有一个图标
    bassmax::StartTray();
    m_trayOn = true;
    if (phr) *phr = S_OK;
}

BassMaxXformFilter::~BassMaxXformFilter() {
    if (m_trayOn) { bassmax::StopTray(); m_trayOn = false; }
}

CUnknown* WINAPI BassMaxXformFilter::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
    BassMaxXformFilter* p = new (std::nothrow) BassMaxXformFilter(pUnk, phr);
    if (!p && phr) *phr = E_OUTOFMEMORY;
    return p;
}

// 只认"音频 + WAVEFORMATEX"。能处理与否由 DSP 自己判断, 不能处理就透传;
// 在这里判死会让 LAV/MPC 的内置解码器切到 DTS/AC3 时直接连不上, 整条链断掉。
HRESULT BassMaxXformFilter::CheckInputType(const CMediaType* mtIn) {
    if (!mtIn) return E_POINTER;
    if (!mtIn->IsValid()) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtIn->Type(), MEDIATYPE_Audio)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtIn->FormatType(), FORMAT_WaveFormatEx)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!mtIn->Format() || mtIn->FormatLength() < sizeof(WAVEFORMATEX))
        return VFW_E_TYPE_NOT_ACCEPTED;
    return S_OK;
}

// 关键差异点 (相对 1.0.3): 不做精确比对。
// 1.0.3 在这里比较 isFloat/位深/声道/采样率, 只要有一项不同就 S_FALSE,
// 而 CTransformInputPin::CheckMediaType 正是拿"新输入类型"和"已连接的旧输出类型"
// 走这个函数 -> 切音轨的 QueryAccept 必然被拒 -> 解码器放弃切换 -> 图卡死。
HRESULT BassMaxXformFilter::CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut) {
    if (!mtIn || !mtOut) return E_POINTER;
    if (!IsEqualGUID(*mtIn->Type(), MEDIATYPE_Audio)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtOut->Type(), MEDIATYPE_Audio)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtIn->FormatType(), FORMAT_WaveFormatEx)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (!IsEqualGUID(*mtOut->FormatType(), FORMAT_WaveFormatEx)) return VFW_E_TYPE_NOT_ACCEPTED;
    return S_OK;
}

// 输出类型 = 当前输入类型 (原样交出去)。这样无论播放器怎么重连输出引脚,
// 拿到的都是此刻真实的格式, 不会出现"上旧下新"。
HRESULT BassMaxXformFilter::GetMediaType(int iPosition, CMediaType* pMediaType) {
    if (!pMediaType) return E_POINTER;
    if (iPosition < 0) return E_INVALIDARG;
    if (iPosition > 0) return VFW_S_NO_MORE_ITEMS;
    if (!m_pInput || !m_pInput->IsConnected()) return VFW_E_NOT_CONNECTED;
    const CMediaType& in = m_pInput->CurrentMediaType();
    if (!in.IsValid()) return VFW_E_INVALIDMEDIATYPE;
    HRESULT hr = pMediaType->Set(in);
    if (FAILED(hr)) return hr;
    return S_OK;
}

HRESULT BassMaxXformFilter::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) {
    if (!pAlloc || !pProps) return E_POINTER;

    // 起点: 下游通过 GetAllocatorRequirements 提的要求 (CBaseOutputPin::DecideAllocator
    // 已经填进来了), 再取 max(上游块长, 100ms 数据量)。块长取小会在高码率 PCM 上
    // 让下游每帧都要重分配, 取大只是多占几 MB 内存。
    long want = pProps->cbBuffer;
    if (want < 0) want = 0;

    if (m_pInput && m_pInput->IsConnected()) {
        IMemAllocator* a = nullptr;
        if (SUCCEEDED(m_pInput->GetAllocator(&a)) && a) {
            ALLOCATOR_PROPERTIES in = {};
            if (SUCCEEDED(a->GetProperties(&in)) && in.cbBuffer > 0 && (long)in.cbBuffer > want)
                want = (long)in.cbBuffer;
            a->Release();
        }
        const CMediaType& mt = m_pInput->CurrentMediaType();
        if (mt.IsValid() && mt.Format() && mt.FormatLength() >= sizeof(WAVEFORMATEX)) {
            const WAVEFORMATEX* wf = reinterpret_cast<const WAVEFORMATEX*>(mt.Format());
            if (wf->nAvgBytesPerSec > 0) {
                const long tenth = (long)(wf->nAvgBytesPerSec / 10);
                if (tenth > want) want = tenth;
            }
        }
    }
    if (want < 16384) want = 16384;

    pProps->cbBuffer = want;
    if (pProps->cBuffers < 2) pProps->cBuffers = 2;
    pProps->cbAlign  = 1;
    pProps->cbPrefix = 0;

    ALLOCATOR_PROPERTIES act = {};
    HRESULT hr = pAlloc->SetProperties(pProps, &act);
    if (FAILED(hr)) return hr;
    if ((long)act.cbBuffer < want) return E_FAIL;   // 拿不到要求的块长就别硬连
    return S_OK;
}

HRESULT BassMaxXformFilter::SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt) {
    HRESULT hr = CTransformFilter::SetMediaType(dir, pmt);
    if (FAILED(hr)) return hr;
    if (dir == PINDIR_INPUT && pmt && pmt->IsValid()) {
        m_dsp.SetFormat(pmt);
        m_hasSampleMt = false;        // 引脚级格式变化后, 采样级的旧类型作废
    }
    return S_OK;
}

// 输入引脚重新连上时, 如果输出引脚还挂着旧格式, 主动把新格式推给下游。
// (CTIP 的 CompleteConnect 内部就是这么干的; CTransformFilter 默认什么都不做,
//  于是 MPC-BE 在暂停状态下重新渲染音频链时会留下"上旧下新"。)
HRESULT BassMaxXformFilter::CompleteConnect(PIN_DIRECTION dir, IPin* pReceivePin) {
    HRESULT hr = CTransformFilter::CompleteConnect(dir, pReceivePin);
    if (FAILED(hr)) return hr;
    if (dir != PINDIR_INPUT) return S_OK;
    if (!m_pInput || !m_pOutput || !m_pOutput->IsConnected()) return S_OK;
    if (m_pInput->CurrentMediaType() == m_pOutput->CurrentMediaType()) return S_OK;
    if (!m_pGraph) return S_OK;       // 不在图里 (自测宿主): 没得重连, 别报错
    return ReconnectPin(m_pOutput, &m_pInput->CurrentMediaType());
}

HRESULT BassMaxXformFilter::Transform(IMediaSample* pIn, IMediaSample* pOut) {
    if (!pIn || !pOut) return S_OK;

    // --- 运行中格式变更: 采样自带媒体类型 (解码器不重连引脚, 只改样本) ---
    bool typeChanged = false;
    AM_MEDIA_TYPE* pmt = nullptr;
    if (S_OK == pIn->GetMediaType(&pmt) && pmt) {
        CMediaType mt(*pmt);
        DeleteMediaType(pmt);
        m_dsp.SetFormat(&mt);
        m_sampleMt = mt;              // 成员里留一份, 指针要活过下游的 Receive
        m_hasSampleMt = true;
        typeChanged = true;
    }

    BYTE* src = nullptr;
    BYTE* dst = nullptr;
    if (FAILED(pIn->GetPointer(&src)) || !src) return S_OK;
    if (FAILED(pOut->GetPointer(&dst)) || !dst) return S_OK;

    long len = pIn->GetActualDataLength();
    if (len < 0) len = 0;
    const long cap = pOut->GetSize();
    if (cap <= 0) return S_OK;

    if (len > cap) {
        // 输出块比输入小: 截断到整帧边界, 剩下的丢掉 (绝不越界写)
        int fb = m_dsp.FrameBytes();
        if (fb <= 0) fb = 1;
        len = (cap / fb) * fb;
    }

    if (len > 0) memcpy(dst, src, (size_t)len);

    if (len > 0 && m_dsp.Processable()) {
        m_dsp.Tick();                 // 注册表参数变了就重建系数
        m_dsp.Process(dst, len);
    }

    pOut->SetActualDataLength(len);
    if (typeChanged) pOut->SetMediaType(static_cast<AM_MEDIA_TYPE*>(&m_sampleMt));
    return S_OK;
}

// ---------- 属性页 ----------
STDMETHODIMP BassMaxXformFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (IsEqualGUID(riid, IID_ISpecifyPropertyPages))
        return GetInterface((ISpecifyPropertyPages*)this, ppv);
    return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP BassMaxXformFilter::GetPages(CAUUID* pPages) {
    if (!pPages) return E_POINTER;
    pPages->pElems = (GUID*)CoTaskMemAlloc(sizeof(GUID));
    if (!pPages->pElems) { pPages->cElems = 0; return E_OUTOFMEMORY; }
    pPages->cElems = 1;
    pPages->pElems[0] = CLSID_BassMaxPage;
    return S_OK;
}
