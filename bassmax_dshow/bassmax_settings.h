// bassmax_settings.h  —  全局参数存储 (属性页 / 托盘图标 / 滤镜实例共用)
#pragma once
#include <windows.h>

namespace bassmax {

struct Settings {
    float bass;    // 低频搁架增益 (dB), -12 .. +12
    float drive;   // 驱动 0 .. 1 (0..10 级, 每级 0.1)
    int   preset;  // EQ 风格 0..5
};

// 幂等: 首次调用时从 HKCU\Software\BassMax 读取 (Bass / Drive 字符串值)
void     InitSettings();

// 线程安全读取
Settings GetSettings();

// 线程安全写入 + 存回注册表; 返回变更代号
LONG     SetSettings(const Settings& s);

// 变更代号: 音频线程用它判断是否需要重建系数
LONG     SettingsGen();

// 是否显示右下角托盘图标 (HKCU\Software\BassMax 的 TrayIcon, 默认 1)
bool     TrayEnabled();

} // namespace bassmax
