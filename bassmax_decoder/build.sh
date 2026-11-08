#!/bin/bash
# 编译 64-bit BassMax 解码器壳 (bassmax_decoder.ax)
#
# 独立 .ax: 不改动 bassmax_dshow.ax 的单管线 CTIP 实现。复用同一份 baseclasses 与
# bassmax_dsp.cpp / bassmax_settings.cpp / bassmax_core.hpp (静态编译进本 DLL,
# 不导出符号, 与另一个 .ax 无 DLL 级冲突)。
set +e
export MSYS_NO_PATHCONV=1
CL="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
SDK="C:/Program Files (x86)/Windows Kits/10"
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
export INCLUDE="$SDK/Include/10.0.26100.0/um;$SDK/Include/10.0.26100.0/shared;$SDK/Include/10.0.26100.0/ucrt;$MSVC/include"
export LIB="$SDK/Lib/10.0.26100.0/um/x64;$SDK/Lib/10.0.26100.0/ucrt/x64;$MSVC/lib/x64"

mkdir -p obj
BASE="../bassmax_dshow/baseclasses"
SRCS=$(ls "$BASE"/*.cpp | grep -v 'dllentry.cpp')
SRCS="$SRCS bassmax_decoder.cpp ../bassmax_dshow/bassmax_dsp.cpp ../bassmax_dshow/bassmax_settings.cpp"
INC="-I$BASE -I../bassmax_dshow -I.."

echo "==== COMPILE ===="
"$CL" /nologo /c /utf-8 /MD /O2 /EHsc /DNDEBUG $INC $SRCS /Foobj/ 2>&1 | tee build_compile.log
echo "compile exit: ${PIPESTATUS[0]}"

echo "==== LINK ===="
"$CL" /nologo /MD /O2 /DNDEBUG /LD /Febassmax_decoder.ax obj/*.obj \
  strmiids.lib uuid.lib ole32.lib oleaut32.lib user32.lib gdi32.lib \
  kernel32.lib advapi32.lib winmm.lib comctl32.lib shell32.lib \
  /link /EXPORT:DllGetClassObject /EXPORT:DllCanUnloadNow \
        /EXPORT:DllRegisterServer /EXPORT:DllUnregisterServer 2>&1 | tee build_link.log
echo "link exit: ${PIPESTATUS[0]}"
ls -l bassmax_decoder.ax 2>/dev/null
echo "BUILD SCRIPT DONE"
