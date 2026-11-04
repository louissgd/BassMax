#!/bin/bash
# 编译 C++ 参考渲染器 (用 bassmax_dshow/bassmax_core.hpp), 供 JS 移植做逐样本对比
set +e
export MSYS_NO_PATHCONV=1
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
export INCLUDE="$SDK/Include/10.0.26100.0/um;$SDK/Include/10.0.26100.0/shared;$SDK/Include/10.0.26100.0/ucrt;$MSVC/include"
export LIB="$SDK/Lib/10.0.26100.0/um/x64;$SDK/Lib/10.0.26100.0/ucrt/x64;$MSVC/lib/x64"
CL="$MSVC/bin/Hostx64/x64/cl.exe"
cd "$(dirname "$0")"
"$CL" /nologo /utf-8 /O2 /EHsc /DNDEBUG /I"../.." ../../render_wav.cpp /Fe:render_ref.exe 2>&1 | tail -5
echo "exit: $?"
ls -l render_ref.exe 2>/dev/null
