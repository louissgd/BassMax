// bassmax_filter.h  —  BassMax 64-bit DirectShow 音频变换滤镜
//
// 包装 bassmax::Core DSP 核心 (bassmax_core.hpp)。
// 输入格式: 8/16/24/32-bit 整数 PCM 与 32/64-bit IEEE float, 1~8 声道, 任意常用采样率。
// 输出格式与输入完全一致 (就地变换, 不重采样、不复制), 声道按对处理 (5.1/7.1 也支持)。
//
// ---- 为什么是 CTransInPlaceFilter 而不是 CTransformFilter (1.0.4) --------------
// 1.0.3 及之前用 CTransformFilter: 输入/输出各一套媒体类型, 输出类型只能由
// GetMediaType 静态生成, 而 CTransformInputPin::CheckMediaType 会拿"新输入类型"
// 去和"已连接但已经过期的输出类型"做 CheckTransform 精确比对。
// LAV 切音轨时 (LAV Splitter -> LAV Audio Decoder 输出 PCM) 会先 QueryAccept 新
// 格式: 声道数/位深/采样率只要和当前那条不同, 精确比对就必然失败 -> 返回 S_FALSE,
// 解码器于是放弃动态切换; 更糟的是万一它硬连成功, 我们的输出引脚仍停留在旧格式,
// 下游渲染器按旧块长收新数据 -> 整条音频链路错位, 播放器判定音频流坏掉 -> 画面
// 一起停住。这就是"多音轨文件切音轨必死, 关了音效也没用"的根因 —— 关音效只是把
// 参数归零, 滤镜本身仍然卡在链路里做格式协商。
//
// CTransInPlaceFilter 天生为"输入=输出、原地改样本"的场合设计:
//   * 输入引脚 CheckMediaType 直接转发下游 QueryAccept -> 下游能接就接, 不再由
//     我们自己拍脑袋拒绝, 切音轨时 LAV 走的就是这条路;
//   * CompleteConnect 发现输入类型和输出类型不一致时, 会自动 ReconnectPin 把新
//     类型推给下游, 不会留下"上旧下新"的半截状态;
//   * 不需要 GetMediaType / CheckTransform / DecideBufferSize, 少两个可出错的地方。
// 另外: 无法处理的格式 (DTS/AC3 passthrough、>8 声道等) 一律原样透传, 绝不返回
// 失败 —— 一个音效滤镜再怎么样都不该把播放器的图搞崩。
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

// 解析后的音频格式 (只描述"能不能处理", 不能处理时 m_processable=false 走透传)
struct BassMaxFmt {
    bool  isFloat = false;
    int   bytes   = 2;      // 容器位深 / 8 (每声道每样本字节数)
    int   ch      = 2;
    DWORD sr      = 44100;
    bool  ok      = false;  // true = 我们改得动这路音频
};

// 从媒体类型解析格式 (纯函数, 便于单测)
BassMaxFmt BassMaxParseFmt(const CMediaType* mt);

class BassMaxFilter : public CTransInPlaceFilter, public ISpecifyPropertyPages {
public:
    BassMaxFilter(LPUNKNOWN pUnk, HRESULT* phr);
    virtual ~BassMaxFilter();

    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr);

    // 多重继承需要显式提供 IUnknown 实现 (同时满足 CTransInPlaceFilter 与 ISpecifyPropertyPages)
    DECLARE_IUNKNOWN;

    // ---- 暴露 ISpecifyPropertyPages, 让播放器能打开"属性"对话框 ----
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP GetPages(CAUUID* pPages) override;

    // ---- 必须实现 ----
    HRESULT CheckInputType(const CMediaType* mtIn) override;
    HRESULT Transform(IMediaSample* pSample) override;
    HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt) override;

    // ---- 参数 (全局共享, 见 bassmax_settings) ----
    void  SetBass(float b);    // dB
    void  SetDrive(float d);   // 0..1
    float GetBass()  const { return m_params.bass; }
    float GetDrive() const { return m_params.drive; }

    void  ApplySettings();     // 从全局设置同步到 DSP

private:
    void ApplyToAllCores();
    // 应用新格式: 重建每个声道对的 Core, 返回是否可处理
    bool SetFormatFromType(const CMediaType* mt);
    // 按 m_ch 补齐 Core 数量并 reset
    void RebuildCores();
    // 保证 scratch 能装下 frames 帧, 失败返回 false (调用方降级为透传)
    bool ReserveScratch(size_t frames);

    std::vector<bassmax::Core> m_cores;   // 每个声道对一份 (2.0 -> 1, 5.1 -> 3, 7.1 -> 4)
    std::vector<float> m_scratch;         // 单对声道的交错缓冲
    bassmax::Params  m_params;
    volatile LONG m_gen = -1;             // 已应用的设置代号

    DWORD m_sr         = 0;               // 采样率
    int   m_ch         = 0;               // 声道数
    int   m_bytes      = 2;               // 每样本字节数 (容器位深/8)
    int   m_frameBytes = 0;               // 一帧字节数 = m_bytes * m_ch
    bool  m_isFloat    = false;           // 是否 IEEE float
    bool  m_processable = false;          // false = 原样透传 (压缩格式 / 声道过多 / 位深怪)
    bool  m_trayOn     = false;
};
