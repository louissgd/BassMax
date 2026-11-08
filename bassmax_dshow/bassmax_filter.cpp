// bassmax_filter.cpp  —  BassMax DirectShow 变换滤镜实现 (CTransInPlaceFilter)
//
// 稳定性要点:
//   * 就地变换: 输入格式 = 输出格式, 不需要输出分配器 / 不需要拷贝样本
//   * 任何"改不动"的格式 (DTS/AC3 passthrough、>8 声道、未知位深) 一律原样透传,
//     Transform 永远返回 S_OK —— 音效滤镜绝不能把播放器的图搞崩
//   * 运行中格式变更 (LAV 切音轨) 有两条入口都处理:
//       1) 输入引脚 SetMediaType -> BassMaxDsp::SetFormat
//       2) 采样自带 AM_MEDIA_TYPE (IMediaSample::GetMediaType) -> 同样重建
//   * 音频线程内不阻塞、不抛异常; 分配失败就降级成透传, 不返回 E_OUTOFMEMORY
//
// DSP 与格式解析搬去 bassmax_dsp.*, 这里只留 CTIP 的胶水。
#include "bassmax_filter.h"
#include "bassmax_settings.h"
#include "bassmax_tray.h"
#include <new>

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

// 把全局设置同步进 DSP (音频线程检测到代号变化后调用; 属性页只写注册表, 不碰 DSP)
void BassMaxFilter::ApplySettings() {
    m_dsp.ApplySettings();
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
    if (dir == PINDIR_INPUT && pmt && pmt->IsValid()) m_dsp.SetFormat(pmt);
    return S_OK;
}

HRESULT BassMaxFilter::Transform(IMediaSample* pSample) {
    if (!pSample) return S_OK;

    // --- 运行中格式变更: 采样上挂着新的媒体类型 (LAV 切音轨走的就是这条路) ---
    AM_MEDIA_TYPE* pmt = nullptr;
    if (S_OK == pSample->GetMediaType(&pmt) && pmt) {
        CMediaType mt(*pmt);
        DeleteMediaType(pmt);
        m_dsp.SetFormat(&mt);
    }

    // 改不动就不改: 原样放过。绝不能在这里返回失败代码。
    if (!m_dsp.Processable()) return S_OK;

    // 用户在属性页/托盘改了参数? 在音频线程里安全地重建系数
    m_dsp.Tick();
    if (!m_dsp.Processable()) return S_OK;      // 重建失败时保底

    // 上游缓冲只读就不碰 (CTIP 需要时已经先复制过了, 这里只是兜底)
    if (InputPin() && InputPin()->ReadOnly() && !UsingDifferentAllocators()) return S_OK;

    BYTE* p = nullptr;
    if (FAILED(pSample->GetPointer(&p)) || !p) return S_OK;

    long len = pSample->GetActualDataLength();
    const long cap = pSample->GetSize();
    if (len <= 0 || cap <= 0) return S_OK;
    if (len > cap) len = cap;                         // 绝不越过实际缓冲区

    m_dsp.Process(p, len);
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
