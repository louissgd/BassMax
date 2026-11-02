#!/bin/bash
# 编译 64-bit BassMax DirectShow 滤镜 (bassmax_dshow.ax)
# 依赖: Microsoft DirectShow baseclasses (本目录 baseclasses/), Windows SDK 10, MSVC 2022
set +e
export MSYS_NO_PATHCONV=1
CL="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
SDK="C:/Program Files (x86)/Windows Kits/10"
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
export INCLUDE="$SDK/Include/10.0.26100.0/um;$SDK/Include/10.0.26100.0/shared;$SDK/Include/10.0.26100.0/ucrt;$MSVC/include"
export LIB="$SDK/Lib/10.0.26100.0/um/x64;$SDK/Lib/10.0.26100.0/ucrt/x64;$MSVC/lib/x64"

mkdir -p obj
# baseclasses 全部编译, 仅排除 dllentry.cpp (其定义 DllMain/类厂, 我们用自己的 dllmain.cpp)
SRCS=$(ls baseclasses/*.cpp | grep -v 'dllentry.cpp')
SRCS="$SRCS bassmax_filter.cpp bassmax_settings.cpp bassmax_page.cpp bassmax_tray.cpp dllmain.cpp"

echo "==== RESOURCE ===="
RC="$SDK/bin/10.0.26100.0/x64/rc.exe"
"$RC" /nologo /fo obj/bassmax.res /i . bassmax.rc 2>&1 | tee build_rc.log
echo "rc exit: ${PIPESTATUS[0]}"

echo "==== COMPILE ===="
"$CL" /nologo /c /utf-8 /MD /O2 /EHsc /DNDEBUG /Ibaseclasses /I. $SRCS /Foobj/ 2>&1 | tee build_compile.log
echo "compile exit: ${PIPESTATUS[0]}"

echo "==== LINK ===="
# 注意: /EXPORT 等链接器选项必须放在 /link 之后, 否则 cl 会忽略 (D9002)
# BassMaxConfig 由 bassmax_tray.cpp 的 __declspec(dllexport) 导出, 不用再写 /EXPORT
"$CL" /nologo /MD /O2 /DNDEBUG /LD /Febassmax_dshow.ax obj/*.obj obj/bassmax.res \
  strmiids.lib uuid.lib ole32.lib oleaut32.lib user32.lib gdi32.lib kernel32.lib advapi32.lib winmm.lib comctl32.lib shell32.lib \
  /link /EXPORT:DllGetClassObject /EXPORT:DllCanUnloadNow /EXPORT:DllRegisterServer /EXPORT:DllUnregisterServer 2>&1 | tee build_link.log
echo "link exit: ${PIPESTATUS[0]}"
ls -l bassmax_dshow.ax 2>/dev/null

# 独立设置入口: 不再单独编译 EXE, 由 .ax 导出的 BassMaxConfig 承担:
#     rundll32.exe bassmax_dshow.ax,BassMaxConfig
echo "BUILD SCRIPT DONE"
