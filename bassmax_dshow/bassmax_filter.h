// bassmax_filter.h  —  BassMax 64-bit DirectShow 音频变换滤镜 (CTransInPlace 版)
//
// 包装 bassmax::Core DSP 核心 (bassmax_core.hpp / bassmax_dsp.hpp)。
// 输入格式: 8/16/24/32-bit 整数 PCM 与 32/64-bit IEEE float, 1~8 声道, 任意常用采样率。
// 输出格式与输入完全一致 (就地变换, 不重采样、不复制), 声道按对处理 (5.1/7.1 也支持)。
//
// ---- 为什么这个版本是 CTransInPlaceFilter (1.0.4 起的决定) ---------------------
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
//
// ---- 1.0.6: 回归单管线 -------------------------------------------------------
// 早期双管线尝试曾为 MPC-BE 在同一 .ax 里再注册一个 CTransformFilter 版 (C9B3)。查
// MPC-BE 官方源码后确认这条路走不通: 内置 Audio Switcher 以最高 merit 无条件
// 占位, 任何 PCM→PCM 外部滤镜都进不了链路 (被创建后立刻销毁)。MPC-BE 现由
// 独立的 bassmax_decoder.ax (C9B4, 解码器角色) 接管, 故 C9B3 已移除。
// 本文件保持 CTIP 不变, 继续服务 PotPlayer / MPC-HC。
#pragma once
#include <streams.h>
#include "bassmax_dsp.h"

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
    float GetBass()  const { return m_dsp.Bass(); }
    float GetDrive() const { return m_dsp.Drive(); }

    void  ApplySettings();     // 从全局设置同步到 DSP

private:
    BassMaxDsp m_dsp;
    bool  m_trayOn = false;
};
