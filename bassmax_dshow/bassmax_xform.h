// bassmax_xform.h  —  [已停用, 保留供追溯] CTransformFilter 版 BassMax
//
// 1.0.6 起不再编入 build.sh、不再注册 (C9B3)。查 MPC-BE 官方源码确认内置
// Audio Switcher 会挡住所有 PCM→PCM 外部滤镜, 这个版本从未真正生效。
// MPC-BE 请改用独立的 bassmax_decoder.ax (C9B4, 解码器角色)。
// 本文件仅作为那次双管线尝试的历史记录, 不要重新启用。
//
// 为什么需要第二个版本:
//   MPC-BE 的外部滤镜插入走 FGManager::ConnectInternal 的第 4 步: 把候选滤镜当作
//   传统 transform 塞进"解码器 -> 渲染器"之间。传统 transform = 独立输出引脚 +
//   自己的输出分配器 (CTransformFilter)。而 CTransInPlaceFilter 的输出引脚没有
//   分配器, 必须把上游 allocator 推给下游, 协商不上就被丢弃 —— 表现正是
//   "mpc-be64.exe 已加载 bassmax_dshow.ax, 但右键->滤镜里没有它"。
//
//   本类 = 1.0.3 那条路, 但把 1.0.3 的三个致命缺陷按 1.0.4 的经验重写:
//     1. GetMediaType 不再静态生成输出类型, 直接把当前输入类型原样交出去;
//     2. CheckTransform 不做精确比对, 只要是 WAVEFORMATEX 音频就 S_OK,
//        这样切音轨时解码器的 QueryAccept 永远通过;
//     3. DecideBufferSize 按输入格式算块长, Transform 遇到超长/异常数据一律
//        memcpy 透传并截断, 永不返回失败。
//
//   DSP 与 BassMaxFilter (CTIP 版) 完全共用 bassmax_dsp.h, 两版音色一致。
#pragma once
#include <streams.h>
#include "bassmax_dsp.h"

class BassMaxXformFilter : public CTransformFilter, public ISpecifyPropertyPages {
public:
    BassMaxXformFilter(LPUNKNOWN pUnk, HRESULT* phr);
    virtual ~BassMaxXformFilter();

    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr);

    // 多重继承 (CTransformFilter + ISpecifyPropertyPages) 需要显式 IUnknown
    DECLARE_IUNKNOWN;

    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP GetPages(CAUUID* pPages) override;

    // ---- CTransformFilter 必实现 ----
    HRESULT CheckInputType(const CMediaType* mtIn) override;
    HRESULT CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut) override;
    HRESULT GetMediaType(int iPosition, CMediaType* pMediaType) override;
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) override;

    // ---- 可选覆盖: 跟踪格式变化 / 输出重连 ----
    HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt) override;
    HRESULT CompleteConnect(PIN_DIRECTION dir, IPin* pReceivePin) override;

    // ---- 数据 ----
    HRESULT Transform(IMediaSample* pIn, IMediaSample* pOut) override;

    void  ApplySettings() { m_dsp.ApplySettings(); }
    float GetBass()  const { return m_dsp.Bass(); }
    float GetDrive() const { return m_dsp.Drive(); }

private:
    BassMaxDsp m_dsp;
    bool       m_trayOn = false;
    // 上游用"采样自带媒体类型"改格式时, 把新类型留一份给下游 (样本上的指针必须
    // 活过本次 Receive, 所以不能是 Transform 里的局部变量)。
    CMediaType m_sampleMt;
    bool       m_hasSampleMt = false;
};
