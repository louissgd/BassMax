#!/bin/bash
# 构建 BassMax VST3。默认使用本地 JUCE: "D:/Program Files/JUCE"
# 可用 JUCE_DIR 环境变量覆盖。仅生成 Release VST3 (及 Standalone)。
set +e
export MSYS_NO_PATHCONV=1
SDK="C:/Program Files (x86)/Windows Kits/10"
MSVC="D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
export INCLUDE="$SDK/Include/10.0.26100.0/um;$SDK/Include/10.0.26100.0/shared;$SDK/Include/10.0.26100.0/ucrt;$MSVC/include"
export LIB="$SDK/Lib/10.0.26100.0/um/x64;$SDK/Lib/10.0.26100.0/ucrt/x64;$MSVC/lib/x64"
export PATH="/d/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64:/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64:$PATH"
CMAKE="D:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
JUCE="${JUCE_DIR:-D:/Program Files/JUCE}"

echo "==== CONFIGURE (NMake, Release) ===="
"$CMAKE" -S . -B build_nm -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$MSVC/bin/Hostx64/x64/cl.exe" \
  -DCMAKE_CXX_COMPILER="$MSVC/bin/Hostx64/x64/cl.exe" \
  -DCMAKE_MAKE_PROGRAM="$MSVC/bin/Hostx64/x64/nmake.exe" \
  -DJUCE_DIR="$JUCE" 2>&1 | tee cfg.log
echo "configure exit: ${PIPESTATUS[0]}"

echo "==== BUILD ===="
"$CMAKE" --build build_nm 2>&1 | tee build.log
echo "build exit: ${PIPESTATUS[0]}"

echo "==== RESULT ===="
find build_nm -iname "*.vst3" -maxdepth 6 2>/dev/null
find build_nm -iname "BassMax*.dll" -maxdepth 8 2>/dev/null | head
