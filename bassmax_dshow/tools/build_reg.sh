#!/bin/bash
# 编译最小注册工具 (tools/reg_helper.exe, 64 位)
# 用途: 替代 regsvr32 (部分环境把 regsvr32/rundll32 列为 LOLBin 直接拦截)
#   tools/reg_helper.exe <path.ax>        注册
#   tools/reg_helper.exe <path.ax> /u     反注册
set +e
export MSYS_NO_PATHCONV=1
CL="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
SDK="C:/Program Files (x86)/Windows Kits/10"
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
export INCLUDE="$SDK/Include/10.0.26100.0/um;$SDK/Include/10.0.26100.0/shared;$SDK/Include/10.0.26100.0/ucrt;$MSVC/include"
export LIB="$SDK/Lib/10.0.26100.0/um/x64;$SDK/Lib/10.0.26100.0/ucrt/x64;$MSVC/lib/x64"

cd "$(dirname "$0")"
"$CL" /nologo /utf-8 /MD /O2 /EHsc /DNDEBUG /Fe:reg_helper.exe /Fo:reg_helper.obj reg_helper.cpp \
  strmiids.lib ole32.lib oleaut32.lib uuid.lib user32.lib 2>&1 | tee build_reg.log
echo "build exit: ${PIPESTATUS[0]}"
ls -l reg_helper.exe 2>/dev/null
