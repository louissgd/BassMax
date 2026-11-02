@echo off
chcp 65001 >nul
set SRC=%~dp0bassmax_dshow\bassmax_dshow.ax
set DST=%USERPROFILE%\MyMPC\Codecs\bassmax_dshow.ax

echo 正在关闭 MPC-HC ...
taskkill /im mpc-hc64.exe /f >nul 2>&1
taskkill /im mpc-hc.exe /f >nul 2>&1
timeout /t 2 >nul

echo 复制新版本 ...
copy /y "%SRC%" "%DST%"
if errorlevel 1 (
  echo.
  echo [失败] 文件被占用。请手动退出所有播放器后重试。
  pause
  exit /b 1
)

echo.
echo [完成] 已更新: %DST%
echo 重新打开播放器即可看到新的界面 (爵士乐 / 摇滚乐 标签已按原版对调)
pause
