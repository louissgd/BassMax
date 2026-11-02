@echo off
chcp 65001 >nul
set SRC=%~dp0bassmax_dshow
set DST=%USERPROFILE%\MyMPC\Codecs
set MASTER=D:\MyMPC_CG_x86_x64\3.Core\1.Core

echo 正在关闭 MPC-HC ...
taskkill /im mpc-hc64.exe /f >nul 2>&1
taskkill /im mpc-hc.exe /f >nul 2>&1
timeout /t 2 >nul

echo 复制到已安装目录 ...
copy /y "%SRC%\bassmax_dshow.ax"  "%DST%\bassmax_dshow.ax"  || goto :fail
rem 旧版附带的独立设置程序已废弃, 顺手清掉
if exist "%DST%\BassMaxConfig.exe" del /q "%DST%\BassMaxConfig.exe"

if exist "%MASTER%" (
  echo 复制到安装包母版 ...
  copy /y "%SRC%\bassmax_dshow.ax"  "%MASTER%\bassmax_dshow.ax"  || goto :fail
  if exist "%MASTER%\BassMaxConfig.exe" del /q "%MASTER%\BassMaxConfig.exe"
)

echo.
echo [完成]
echo   滤镜 : %DST%\bassmax_dshow.ax
echo   设置 : rundll32.exe "%DST%\bassmax_dshow.ax",BassMaxConfig
pause
exit /b 0

:fail
echo.
echo [失败] 文件被占用。请手动退出所有播放器后重试。
pause
exit /b 1
