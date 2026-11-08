// bassmax_dsp.h  —  BassMax 的 DSP 包装层 + CLSID / 格式解析
//
// 1.0.6 起回归单管线: 一个 .ax 只注册一个滤镜
//   CLSID_BassMax  CTransInPlaceFilter  "BassMax Audio Enhancer"
//                  -> PotPlayer / MPC-HC 等支持 in-place 变换的播放器
//                  -> 输入=输出同一块缓冲, 不需要输出分配器, 切音轨最稳
//   MPC-BE 由独立的 bassmax_decoder.ax (C9B4, 解码器角色) 负责, 见 README 六·补。
//
// DSP 与 DirectShow 缓冲模型解耦, 因此两个 .ax 都能共用本文件的实现。
#pragma once
#include <streams.h>
#include <vector>
#include "bassmax_core.hpp"

// ---- CLSID ------------------------------------------------------------------
// 本滤镜 CLSID (64-bit, CTIP 版)。所有播放器的外部滤镜配置都记着它, 永不改动。
static const GUID CLSID_BassMax = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb1}
};
// 早期双管线尝试曾用过的 CTransform 版 (试图给 MPC-BE), 已停用。官方源码确认 PCM→PCM
// 外部滤镜进不了 MPC-BE 音频链, 这条管线从未真正生效。保留常量仅供
// DllRegisterServer / DllUnregisterServer 清理旧注册表残留; 不再注册、不再实例化,
// 也不要复用这个 CLSID。
static const GUID CLSID_BassMaxXform = {
    0xc41f7d22, 0x6b18, 0x4a93, {0x8e,0x52,0x1d,0x7a,0x4f,0x60,0xc9,0xb3}
};
// 属性页 CLSID (同一张属性页 / 同一份注册表参数)
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

// 解析后的音频格式 (只描述"能不能处理", 不能处理时 ok=false 走透传)
struct BassMaxFmt {
    bool  isFloat = false;
    int   bytes   = 2;      // 容器位深 / 8 (每声道每样本字节数)
    int   ch      = 2;
    DWORD sr      = 44100;
    bool  ok      = false;  // true = 我们改得动这路音频
};

// 从媒体类型解析格式 (纯函数, 便于单测)
BassMaxFmt BassMaxParseFmt(const CMediaType* mt);

// ---------------------------------------------------------------------------
// BassMaxDsp —— 与 DirectShow 缓冲模型无关的处理单元
//
// 用法 (bassmax_dshow.ax 与 bassmax_decoder.ax 一样):
//   SetFormat(mt)   输入格式变了 -> 重建每个声道对的 Core
//   Tick()          音频线程每块调一次, 注册表参数变了就重建系数
//   Process(p, len) 就地处理 len 字节的 PCM; 返回 false = 一个样本都没动
// ---------------------------------------------------------------------------
class BassMaxDsp {
public:
    // 把全局设置同步进 DSP (属性页/托盘只写注册表, 这里才落到系数上)
    void ApplySettings();

    // 设置输入格式。返回是否可处理 (false = 后续全部透传)
    bool SetFormat(const CMediaType* mt);

    // 音频线程: 检测参数代号变化
    void Tick();

    // 就地处理。任何"改不动"的情况 (格式不支持/分配失败/数据不整帧) 返回 false,
    // 调用方直接放行原数据即可 —— 绝不抛异常、绝不返回失败码。
    bool Process(BYTE* p, long len);

    bool  Processable() const { return m_processable; }
    int   Channels()    const { return m_ch; }
    int   FrameBytes()  const { return m_frameBytes; }
    DWORD SampleRate()  const { return m_sr; }
    float Bass()        const { return m_params.bass; }
    float Drive()       const { return m_params.drive; }

private:
    void ApplyToAllCores();
    void RebuildCores();
    bool ReserveScratch(size_t frames);

    std::vector<bassmax::Core> m_cores;   // 每个声道对一份 (2.0 -> 1, 5.1 -> 3, 7.1 -> 4)
    std::vector<float> m_scratch;         // 单对声道的交错缓冲
    bassmax::Params  m_params;
    volatile LONG m_gen = -1;             // 已应用的设置代号

    DWORD m_sr         = 0;
    int   m_ch         = 0;
    int   m_bytes      = 2;
    int   m_frameBytes = 0;
    bool  m_isFloat    = false;
    bool  m_processable = false;
};
