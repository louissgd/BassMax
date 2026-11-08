@echo off
chcp 936 >nul
setlocal
set "AX=%~dp0bassmax_dshow.ax"

net session >nul 2>&1
if errorlevel 1 (
    echo [错误] 需要管理员权限。请右键本文件 - 以管理员身份运行。
    pause & exit /b 1
)

if exist "%AX%" (
    "%SystemRoot%\System32\regsvr32.exe" /u /s "%AX%"
) else (
    echo [提示] 当前目录没有 .ax，请把 .ax 放回本目录再运行本脚本。
    pause & exit /b 1
)

echo 已卸载 BassMax Audio Enhancer（含早期双管线尝试遗留的 MPC-BE 版条目 C9B3）。
echo 若要卸载 MPC-BE 专用解码器壳，请另外运行 bassmax_decoder\uninstall.bat。
pause
