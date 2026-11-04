@echo off
rem ===========================================================================
rem  bmx.cmd - BassMax transcoder wrapper (decode -> bassmax_cli -> encode/mux)
rem
rem  Usage:
rem    bmx.cmd <input>                  out = <name>.bmx<ext> next to input
rem    bmx.cmd <input> -o <output>      explicit output
rem    bmx.cmd <input> [bassmax opts]   e.g. --drive 8.5 --bass 3 --preset jazz
rem
rem  Video containers: video stream is copied untouched, audio is re-encoded.
rem  Audio containers: audio only.
rem
rem  Examples:
rem    bmx.cmd "D:\MV\old.mkv"
rem    bmx.cmd music.flac --drive 9 --bass 4 --preset rock
rem    bmx.cmd clip.mp4 -o "D:\out\clip_fixed.mp4" --drive 7
rem ===========================================================================
setlocal EnableDelayedExpansion

set "FF=D:\Program Files\ffmpeg\ffmpeg.exe"
set "CLI=%~dp0bassmax_cli.exe"

if not exist "%FF%" set "FF=ffmpeg.exe"
if not exist "%CLI%" (
  echo [bmx] bassmax_cli.exe not found next to bmx.cmd
  exit /b 1
)

set "IN="
set "OUT="
set "OPTS="

:parse
if "%~1"=="" goto ready
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help
if /i "%~1"=="-o" (
  set "OUT=%~2"
  shift
  shift
  goto parse
)
if not defined IN (
  set "IN=%~1"
  shift
  goto parse
)
set "OPTS=!OPTS! %1"
shift
goto parse

:ready
if not defined IN goto help
if not exist "%IN%" (
  echo [bmx] input not found: %IN%
  exit /b 3
)

for %%F in ("%IN%") do (
  set "DRV=%%~dF"
  set "DIR=%%~pF"
  set "NAM=%%~nF"
  set "EXT=%%~xF"
)
if not defined OUT set "OUT=%DRV%%DIR%%NAM%.bmx%EXT%"

rem ---- 编码参数按"输出"容器的后缀选 (不是输入后缀) ----
for %%F in ("%OUT%") do set "OEXT=%%~xF"
set "ACODEC=-c:a aac -b:a 320k"
set "AUDIO_ONLY="
set "MOVFLAGS="
if /i "!OEXT!"==".mkv"  set "ACODEC=-c:a flac"
if /i "!OEXT!"==".webm" set "ACODEC=-c:a libopus -b:a 192k"
if /i "!OEXT!"==".mp4"  set "MOVFLAGS=-movflags +faststart"
if /i "!OEXT!"==".mov"  set "MOVFLAGS=-movflags +faststart"
if /i "!OEXT!"==".wav"  ( set "ACODEC=-c:a pcm_s24le" & set "AUDIO_ONLY=1" )
if /i "!OEXT!"==".flac" ( set "ACODEC=-c:a flac"     & set "AUDIO_ONLY=1" )
if /i "!OEXT!"==".mp3"  ( set "ACODEC=-c:a libmp3lame -b:a 320k" & set "AUDIO_ONLY=1" )
if /i "!OEXT!"==".m4a"  ( set "ACODEC=-c:a aac -b:a 320k" & set "AUDIO_ONLY=1" )
if /i "!OEXT!"==".aac"  ( set "ACODEC=-c:a aac -b:a 320k" & set "AUDIO_ONLY=1" )
if /i "!OEXT!"==".ogg"  ( set "ACODEC=-c:a libvorbis -q:a 8" & set "AUDIO_ONLY=1" )
if /i "!OEXT!"==".opus" ( set "ACODEC=-c:a libopus -b:a 192k" & set "AUDIO_ONLY=1" )

set "TMPWAV=%TEMP%\bmx_%RANDOM%%RANDOM%.wav"

rem ---- 时长 (解析 ffmpeg -i 的 Duration 行) -> 进度百分比 ----
set "DUR="
set "DTMP=%TEMP%\bmx_dur_%RANDOM%%RANDOM%.txt"
"%FF%" -hide_banner -i "%IN%" >nul 2>"!DTMP!"
for /f "tokens=2 delims=, " %%a in ('findstr /c:"Duration:" "!DTMP!" 2^>nul') do (
  if not defined DUR set "DUR=%%a"
)
if exist "!DTMP!" del /q "!DTMP!" >nul 2>nul
if defined DUR echo !DUR!| findstr /r "^[0-9][0-9]*:[0-9][0-9]*:[0-9]" >nul || set "DUR="
set "TOTAL="
if defined DUR set "TOTAL=--total !DUR!"

echo [bmx] %IN%
echo [bmx]  -^> %OUT%

rem ---- 1) decode to WAV pipe -> bassmax -> temp wav ----
"%FF%" -v error -nostdin -i "%IN%" -map 0:a:0 -vn -f wav -c:a pcm_f32le - 2>nul | "%CLI%" - !TOTAL! -o "%TMPWAV%" !OPTS!
if errorlevel 1 (
  echo [bmx] failed: decode or bassmax stage ^(no audio stream?^)
  if exist "%TMPWAV%" del /q "%TMPWAV%"
  exit /b 4
)

rem ---- 2) encode / mux ----
if defined AUDIO_ONLY (
  "%FF%" -v error -nostdin -y -i "%TMPWAV%" !ACODEC! -map_metadata 0 "%OUT%"
) else (
  "%FF%" -v error -nostdin -y -i "%IN%" -i "%TMPWAV%" -map 0:v:0? -map 1:a:0 -c:v copy !ACODEC! !MOVFLAGS! -map_metadata 0 -map_chapters 0 "%OUT%"
)
set "RC=%ERRORLEVEL%"

del /q "%TMPWAV%" 2>nul
if not "%RC%"=="0" (
  echo [bmx] failed: encode stage, rc=%RC%
  exit /b 5
)
echo [bmx] done: %OUT%
exit /b 0

:help
echo Usage: bmx.cmd ^<input^> [-o ^<output^>] [bassmax options]
echo.
echo   --drive ^<0..10^>     bass/level stretch amount (0 = bypass)
echo   --bass  ^<dB^>        -12..12, 100Hz shelf
echo   --treble ^<dB^>       -12..12, 9kHz shelf
echo   --preset ^<name^>     standard^|classical^|club^|pop^|jazz^|rock
echo   --makeup ^<dB^>       0..12, output gain (0 = default 2.5)
echo   --out-bits ^<n^>      16^|24^|32^|f32   (wav output only)
echo.
echo   Run bassmax_cli.exe --help for the full list.
exit /b 2
