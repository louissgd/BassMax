// bassmax_tray.h  —  右下角托盘图标 (双击打开设置)
#pragma once
#include <windows.h>

namespace bassmax {

void StartTray();          // 引用计数 +1; 第一个调用者创建图标
void StopTray();           // 引用计数 -1; 归零时移除图标并退出线程
void ShowTraySettings();   // 打开设置窗口 (已打开则前置)

} // namespace bassmax
