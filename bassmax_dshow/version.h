// version.h — 版本号唯一来源 (rc 的 VERSIONINFO 与 C++ 代码都从这里取)
#pragma once

#define BASSMAX_VER_MAJOR 1
#define BASSMAX_VER_MINOR 0
#define BASSMAX_VER_PATCH 1
#define BASSMAX_VER_BUILD 0

// "1.0.1" —— 用于在日志/命令行里报版本
#define BASSMAX_VER_STR "1.0.1"
#define BASSMAX_VER_STR_W L"1.0.1"

// "1.0.1.0" —— VERSIONINFO 里 FileVersion / ProductVersion 的四段式文本
// (rc.exe 的预处理器不支持 ## 拼接, 所以这里单独写一份; 改版本时四处一起改)
#define BASSMAX_VER_STR4 "1.0.1.0"
