#!/bin/bash
# 编译 64-bit 测试宿主 (test/test_host.exe)
#
# 必须与 bassmax_dshow.ax 位数一致: 32 位宿主 LoadLibrary 64 位 .ax 直接 193。
# 用法:
#   bash test/build_test.sh                 # 在 bassmax_dshow 目录下执行
#   test/test_host.exe bassmax_dshow.ax
set +e
export MSYS_NO_PATHCONV=1
CL="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
SDK="C:/Program Files (x86)/Windows Kits/10"
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
export INCLUDE="$SDK/Include/10.0.26100.0/um;$SDK/Include/10.0.26100.0/shared;$SDK/Include/10.0.26100.0/ucrt;$MSVC/include"
export LIB="$SDK/Lib/10.0.26100.0/um/x64;$SDK/Lib/10.0.26100.0/ucrt/x64;$MSVC/lib/x64"

cd "$(dirname "$0")"

"$CL" /nologo /utf-8 /MD /O2 /EHsc /DNDEBUG /Fe:test_host.exe /Fo:test_host.obj test_host.cpp \
  strmiids.lib ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib 2>&1 | tee build_test.log
echo "test build exit: ${PIPESTATUS[0]}"
ls -l test_host.exe 2>/dev/null
