#!/bin/bash
# 编译 bassmax_cli.exe  (MSVC 2022, /MT 静态链接, 单文件, 无外部依赖)
# 依赖: Visual Studio + Windows SDK; DSP 核心来自 ../bassmax_dshow/bassmax_core.hpp
set +e
export MSYS_NO_PATHCONV=1
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
export INCLUDE="$SDK/Include/10.0.26100.0/um;$SDK/Include/10.0.26100.0/shared;$SDK/Include/10.0.26100.0/ucrt;$MSVC/include"
export LIB="$SDK/Lib/10.0.26100.0/um/x64;$SDK/Lib/10.0.26100.0/ucrt/x64;$MSVC/lib/x64"
CL="$MSVC/bin/Hostx64/x64/cl.exe"

cd "$(dirname "$0")"
mkdir -p obj dist

echo "==== COMPILE ===="
"$CL" /nologo /c /utf-8 /MT /O2 /EHsc /GS- /DNDEBUG /W3 \
  /I"../bassmax_dshow" bassmax_cli.cpp /Fo:obj/ 2>&1 | tee build.log
rc=${PIPESTATUS[0]}
echo "compile exit: $rc"
[ $rc -ne 0 ] && exit 1

echo "==== LINK ===="
"$CL" /nologo /MT /O2 /DNDEBUG obj/bassmax_cli.obj /Fedist/bassmax_cli.exe \
  kernel32.lib user32.lib /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF 2>&1 | tee -a build.log
echo "link exit: $?"
ls -l dist/bassmax_cli.exe 2>/dev/null
