@echo off
chcp 936 >nul
setlocal
rem 卸载 BassMax Audio Decoder (LAV Wrapper): 删除 MPC-BE 外部滤镜项 + 反注册 .ax
set "AX=%~dp0bassmax_decoder.ax"
set "HELPER=%~dp0reg_helper.exe"
set "CLSID={C41F7D22-6B18-4A93-8E52-1D7A4F60C9B4}"

net session >nul 2>&1
if errorlevel 1 (
    echo [错误] 需要管理员权限。请右键 - 以管理员身份运行。
    pause & exit /b 1
)

echo [1/2] 删除 MPC-BE 外部滤镜配置 001
reg query "HKCU\Software\MPC-BE\ExternalFilters\001" /v CLSID 2>nul | findstr /i "%CLSID%" >nul
if not errorlevel 1 (
    reg delete "HKCU\Software\MPC-BE\ExternalFilters\001" /f >nul
    echo       已删除 001
) else (
    echo       001 不是本滤镜, 保持原样
)

echo [2/2] 反注册 .ax
if not exist "%AX%" (
    echo [提示] 当前目录没有 bassmax_decoder.ax, 只删除了播放器配置。
    pause & exit /b 0
)
if exist "%HELPER%" (
    "%HELPER%" "%AX%" /u
) else (
    "%SystemRoot%\System32\regsvr32.exe" /u /s "%AX%"
)

echo.
echo 已卸载。MPC-BE 会回落到 MPC Audio Decoder; BassMax DSP 不再进入链路。
echo 若之前手动停用过 000, 需要恢复时把其 Enabled 改回 1 即可。
echo.
pause
