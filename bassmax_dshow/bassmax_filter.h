// bassmax_filter.h  —  BassMax 64-bit DirectShow 音频变换滤镜
//
// 包装 bassmax::Core DSP 核心 (bassmax_core.hpp)。
// 输入格式: 8/16/24/32-bit 整数 PCM 与 32-bit IEEE float, 1~8 声道, 任意常用采样率。
// 输出格式与输入完全一致 (透传式变换, 不重采样), 声道按对处理 (5.1/7.1 也支持)。
#pragma once
#include <streams.h>
#include <vector>
#include "bassmax_core.hpp"

// 本滤镜 CLSID (64-bit)。注册到 HKCR\CLSID 供 64 位播放器使用。
static const GUID CLSID_BassMax = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1}
};
// 属性页 CLSID (同一个 .ax 里导出, 播放器按此 CLSID 创建属性页)
static const GUID CLSID_BassMaxPage = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb2}
};

// ---- 多声道保护 --------------------------------------------------------------
// 实测 (7.1 + 耳机): 滤镜确实生效, 但听感发糊、一直顶着上限。原因是结构性的 ——
// 每对声道各自把动态拉到接近满电平, 下混求和后必然远超 0 dBFS, 限幅器反复触发。
// 所以输入 > 2 声道时把驱动自动压到上限。
//   * 只压不抬: 用户本来就设得比 60% 低, 原样保留;
//   * 注册表里的值一个字节都不改: 切回立体声立刻恢复用户原本的驱动;
//   * 单独提成纯函数是为了能脱离 DirectShow 直接单测。
static const float kBassMaxMultiChDriveCap = 0.60f;

inline float BassMaxEffectiveDrive(float drive, int channels) {
    if (channels > 2 && drive > kBassMaxMultiChDriveCap) return kBassMaxMultiChDriveCap;
    return drive;
}

// KSDATAFORMAT 子类型 (用于解析 WAVEFORMATEXTENSIBLE, 避免链接 initguid)。
static const GUID KSDATAFORMAT_SUBTYPE_PCM_GUID = {
    0x00000001, 0x0000, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}
};
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_GUID = {
    0x00000003, 0x0000, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}
};

class BassMaxFilter : public CTransformFilter, public ISpecifyPropertyPages {
public:
    BassMaxFilter(LPUNKNOWN pUnk, HRESULT* phr);
    virtual ~BassMaxFilter();

    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr);

    // 多重继承需要显式提供 IUnknown 实现 (同时满足 CTransformFilter 与 ISpecifyPropertyPages)
    DECLARE_IUNKNOWN;

    // ---- 暴露 ISpecifyPropertyPages, 让播放器能打开"属性"对话框 ----
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP GetPages(CAUUID* pPages) override;

    // ---- CTransformFilter 必须实现的虚函数 ----
    HRESULT CheckInputType(const CMediaType* mtIn) override;
    HRESULT CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut) override;
    HRESULT DecideBufferSize(IMemAllocator* pAllocator, ALLOCATOR_PROPERTIES* pProp) override;
    HRESULT GetMediaType(int iPosition, CMediaType* pMediaType) override;
    HRESULT Transform(IMediaSample* pIn, IMediaSample* pOut) override;
    HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt) override;

    // ---- 参数 (全局共享, 见 bassmax_settings) ----
    void  SetBass(float b);    // dB
    void  SetDrive(float d);   // 0..1
    float GetBass()  const { return m_params.bass; }
    float GetDrive() const { return m_params.drive; }

    void  ApplySettings();     // 从全局设置同步到 DSP

private:
    void ApplyToAllCores();

    std::vector<bassmax::Core> m_cores;   // 每个声道对一份 (2.0 -> 1, 5.1 -> 3, 7.1 -> 4)
    std::vector<float> m_scratch;         // 单对声道的交错缓冲
    bassmax::Params  m_params;
    volatile LONG m_gen = -1;             // 已应用的设置代号
    DWORD  m_sr   = 0;                    // 采样率
    int    m_ch   = 2;                    // 声道数
    int    m_bytes= 2;                    // 每样本字节数 (容器位深/8)
    bool   m_float= false;                // 是否 IEEE float
    CMediaType m_inputMt;                 // 输入连接后的媒体类型 (输出提案据此生成)
    bool   m_inputSet = false;
    bool   m_trayOn   = false;
};
