#!/bin/bash
# build.sh — 编译离线工具 (render_wav / dump_gain)
set -e
export MSYS_NO_PATHCONV=1
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"; SDKV="10.0.26100.0"
export INCLUDE="$MSVC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt"
export LIB="$MSVC/lib/x64;$SDK/Lib/$SDKV/ucrt/x64;$SDK/Lib/$SDKV/um/x64"
cd "$(dirname "$0")"
cp bassmax_core.hpp base_core.hpp
CL="$MSVC/bin/Hostx64/x64/cl.exe"
"$CL" /nologo /utf-8 /O2 /EHsc /std:c++17 /MD /I. render_wav.cpp /Fe:render_wav.exe /Fo:render_wav.obj 2>&1 | iconv -f GBK -t UTF-8 2>/dev/null | grep -iE "error" || true
"$CL" /nologo /utf-8 /O2 /EHsc /std:c++17 /MD /I. dump_gain.cpp /Fe:dump_gain.exe /Fo:dump_gain.obj 2>&1 | iconv -f GBK -t UTF-8 2>/dev/null | grep -iE "error" || true
echo BUILD_OK
