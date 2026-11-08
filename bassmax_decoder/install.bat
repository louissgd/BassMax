@echo off
chcp 936 >nul
setlocal
rem ============================================================================
rem  BassMax Audio Decoder (LAV Wrapper) 安装脚本
rem  用法: 把本脚本、bassmax_decoder.ax、reg_helper.exe 一起放到目标目录
rem        (例如 MyMPC\Codecs\), 右键"以管理员身份运行"。
rem  作用: 1) 注册 .ax  2) 在 MPC-BE 外部滤镜里把它设为"首选音频解码器"
rem  前置: 系统里必须已注册 LAV Audio Decoder (MyMPC\MediaPlayer\LAVFilters64\LAVAudio.ax)
rem ============================================================================
set "AX=%~dp0bassmax_decoder.ax"
set "HELPER=%~dp0reg_helper.exe"
set "CLSID={C41F7D22-6B18-4A93-8E52-1D7A4F60C9B4}"
rem 幂等: MPC-BE 退出时会重排外部滤镜列表 (禁用项被丢弃, 001 可能变成 000)。
rem 硬编码 001 重装会留下两条同 CLSID 记录, 让同一滤镜被创建两次。
rem 先复用已有的 C9B4 条目, 没有再挑第一个空闲编号。
set "EXT="
for /L %%i in (0,1,9) do (
    if not defined EXT (
        reg query "HKCU\Software\MPC-BE\ExternalFilters\00%%i" /v CLSID 2>nul | findstr /i /c:"%CLSID%" >nul
        if not errorlevel 1 set "EXT=HKCU\Software\MPC-BE\ExternalFilters\00%%i"
    )
)
if not defined EXT (
    for /L %%i in (0,1,9) do (
        if not defined EXT (
            reg query "HKCU\Software\MPC-BE\ExternalFilters\00%%i" >nul 2>&1
            if errorlevel 1 set "EXT=HKCU\Software\MPC-BE\ExternalFilters\00%%i"
        )
    )
)
if not defined EXT set "EXT=HKCU\Software\MPC-BE\ExternalFilters\009"

if not exist "%AX%" (
    echo [错误] 找不到 bassmax_decoder.ax, 请把本脚本和 .ax 放在同一目录。
    pause & exit /b 1
)
net session >nul 2>&1
if errorlevel 1 (
    echo [错误] 需要管理员权限。请右键 - 以管理员身份运行。
    pause & exit /b 1
)

echo [1/3] 注册滤镜: %AX%
if exist "%HELPER%" (
    "%HELPER%" "%AX%"
) else (
    rem reg_helper.exe 不在时用系统 regsvr32 兜底 (部分安全策略会拦截 LOLBin)
    "%SystemRoot%\System32\regsvr32.exe" /s "%AX%"
)
reg query "HKCR\CLSID\%CLSID%\InprocServer32" >nul 2>&1
if errorlevel 1 (
    echo [错误] 注册后仍查不到 CLSID %CLSID%
    echo        若被安全软件拦截, 请改用随附的 reg_helper.exe。
    pause & exit /b 1
)

echo [2/3] 写入 MPC-BE 外部滤镜配置: %EXT%  (LoadType=0 PREFERRED)
reg add "%EXT%" /v SourceType   /t REG_DWORD /d 0 /f >nul
reg add "%EXT%" /v Enabled      /t REG_DWORD /d 1 /f >nul
reg add "%EXT%" /v LoadType     /t REG_DWORD /d 0 /f >nul
reg add "%EXT%" /v Merit        /t REG_DWORD /d 4194304 /f >nul
reg add "%EXT%" /v CLSID        /t REG_SZ /d "%CLSID%" /f >nul
reg add "%EXT%" /v Name         /t REG_SZ /d "BassMax Audio Decoder (LAV Wrapper)" /f >nul
reg add "%EXT%" /v DisplayName  /t REG_SZ /d "@device:sw:{083863F1-70DE-11D0-BD40-00A0C911CE86}\%CLSID%" /f >nul
rem 通配音频输入: major=MEDIATYPE_Audio, subtype=GUID_NULL => 所有压缩音频
reg add "%EXT%" /v MT_000       /t REG_SZ /d "{73647561-0000-0010-8000-00AA00389B71}{00000000-0000-0000-0000-000000000000}" /f >nul

echo [3/3] 停用在 MPC-BE 里无效的旧 transform 项 (000, C9B1)
reg query "HKCU\Software\MPC-BE\ExternalFilters\000" /v CLSID 2>nul | findstr /i "C9B1}" >nul
if not errorlevel 1 (
    reg add "HKCU\Software\MPC-BE\ExternalFilters\000" /v Enabled /t REG_DWORD /d 0 /f >nul
    echo       已停用 000
) else (
    echo       000 不是 BassMax transform, 保持原样
)

echo.
echo 安装完成。请完全退出 MPC-BE 再重新打开 (它只在启动时读取外部滤镜列表)。
echo 播放任意音频后, 在 "播放 - 滤镜" 里应看到:
echo     BassMax Audio Decoder (LAV Wrapper)   取代 MPC Audio Decoder
echo 诊断日志: %%TEMP%%\bassmax_decoder.log  (看 STAT 里 changed 增长且 enabled=1 才算 DSP 真在处理)
echo 参数仍由 BassMax 设置页/托盘控制, 写入 HKCU\Software\BassMax, 本滤镜每 400ms 回读。
echo.
pause
