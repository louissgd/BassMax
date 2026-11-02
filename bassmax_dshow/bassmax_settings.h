// bassmax_settings.h  —  全局参数存储 (属性页 / 托盘图标 / 滤镜实例 / 独立设置程序共用)
#pragma once
#include <windows.h>

namespace bassmax {

struct Settings {
    float bass;    // 低频搁架增益 (dB), -12 .. +12
    float treble;  // 高频搁架增益 (dB), -12 .. +12
    float drive;   // 驱动 0 .. 1 (0..10 级, 每级 0.1)
    int   preset;  // EQ 风格 0..5
    bool  enabled; // 总开关: false = 整条链路旁路, 一个样本都不动
};

// 幂等: 首次调用时从 HKCU\Software\BassMax 读取, 并把缺的值(含 TrayIcon)显性写回,
// 这样注册表里始终能看到完整的一组键, 可以直接手改。
void     InitSettings();

// 线程安全读取
Settings GetSettings();

// 线程安全写入 + 存回注册表; 返回变更代号
LONG     SetSettings(const Settings& s);

// 变更代号: 音频线程用它判断是否需要重建系数
LONG     SettingsGen();

// 是否显示右下角托盘图标 (HKCU\Software\BassMax 的 TrayIcon, 默认 1)
bool     TrayEnabled();

// 重新从注册表读一遍 (供托盘线程定时轮询):
// 有变化则套用并递增变更代号 -> 音频线程下一拍就生效; 无变化返回当前代号。
// 值不全(用户正改到一半)时本次不采纳, 避免把参数刷成默认。
LONG     ReloadFromRegistry();

} // namespace bassmax
