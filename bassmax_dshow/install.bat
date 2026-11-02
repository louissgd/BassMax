@echo off
chcp 65001 >nul
setlocal
set "AX=%~dp0bassmax_dshow.ax"
set "CLSID={C41F7D22-6B18-4A93-8E52-1D7A4F60C9B1}"

if not exist "%AX%" (
    echo [错误] 找不到 bassmax_dshow.ax，请把本脚本和 .ax 放在同一目录。
    pause & exit /b 1
)

net session >nul 2>&1
if errorlevel 1 (
    echo [错误] 需要管理员权限。请右键本文件 - 以管理员身份运行。
    pause & exit /b 1
)

echo 正在注册: %AX%
"%SystemRoot%\System32\regsvr32.exe" /s "%AX%"
if errorlevel 1 (
    echo [错误] regsvr32 返回失败。
    echo 手动执行可看到具体原因:  regsvr32 "%AX%"
    pause & exit /b 1
)

reg query "HKCR\CLSID\%CLSID%\InprocServer32" >nul 2>&1
if errorlevel 1 (
    echo [错误] 注册命令执行了，但注册表里没有写进去。
    echo 请以管理员身份重试，或检查杀毒软件拦截。
    pause & exit /b 1
)

echo.
echo 注册成功。
echo   显示名: BassMax Audio Enhancer
echo   路径  : %AX%
echo.
echo 现在打开 MPC-HC / PotPlayer:
echo   MPC-HC   : 选项 - 外部滤镜 - 添加滤镜, 选中 BassMax Audio Enhancer, 勾选"优先"
echo   PotPlayer: 按 F5 - 滤镜 - 音频滤镜 - 添加, 选中后设为"始终使用"
echo 若列表里还没出现, 请先完全退出播放器再打开(播放器只在启动时读取滤镜列表)。
echo.
pause
