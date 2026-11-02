@echo off
chcp 65001 >nul
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
    echo [提示] 当前目录没有 .ax，改用系统 regsvr32 按 CLSID 无法卸载，请把 .ax 放回本目录再运行本脚本。
    pause & exit /b 1
)

echo 已卸载 BassMax Audio Enhancer。
pause
