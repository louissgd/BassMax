@echo off
chcp 936 >nul
setlocal
set "AX=%~dp0bassmax_dshow.ax"
set "CLSID_TIP={C41F7D22-6B18-4A93-8E52-1D7A4F60C9B1}"

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
rem 1.0.6 起一个 .ax 只有一个滤镜 CLSID；注册时会自动清除早期双管线尝试遗留的 C9B3 幽灵条目
"%SystemRoot%\System32\regsvr32.exe" /s "%AX%"
if errorlevel 1 (
    echo [错误] 注册调用返回失败。
    echo 手动执行可看到具体原因:  regsvr32 "%AX%"
    pause & exit /b 1
)

reg query "HKCR\CLSID\%CLSID_TIP%\InprocServer32" >nul 2>&1
if errorlevel 1 (
    echo [错误] 注册命令执行了，但 CLSID 没写进注册表。
    echo 请以管理员身份重试，或检查杀毒软件拦截。
    pause & exit /b 1
)

echo.
echo 注册成功（一个滤镜 + 一个属性页）。
echo   路径: %AX%
echo.
echo 在各播放器里添加为外部/系统音频滤镜:
echo   MPC-HC   : 选项 - 外部滤镜 - 添加滤镜, 选 "BassMax Audio Enhancer", 勾选"优先"
echo   PotPlayer: F5 - 滤镜 - 音频滤镜 - 添加系统滤镜, 选 "BassMax Audio Enhancer"
echo   MPC-BE   : 不要用本滤镜！请改用 bassmax_decoder.ax
echo              （MPC-BE 内置 Audio Switcher 会挡住所有 PCM 外部滤镜,
echo                必须用"解码器角色"的外壳, 见 bassmax_decoder\install.bat）
echo 若列表里还没出现, 请先完全退出播放器再打开（播放器只在启动时读取滤镜列表）。
echo.
echo 播放器没开时也能改设置 —— 不需要额外 EXE, 走系统自带的 rundll32:
echo    rundll32.exe "%AX%",BassMaxConfig
echo.
pause
