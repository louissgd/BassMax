@echo off
rem ===========================================================================
rem  bmrun.cmd - one shot: probe duration -> ffmpeg decode (f32) ->
rem              bassmax_cli (with --total, so the progress bar has a %)
rem
rem  Usage:
rem    bmrun.cmd <input> [-o <output>] [--af "<filter>"] [bassmax options]
rem
rem  Why: ffmpeg writing wav to a pipe cannot fill in the length fields, so
rem  bassmax_cli has no way to know the total and can only show elapsed time.
rem  This wrapper reads the duration from ffmpeg itself and passes --total.
rem  It also decodes to pcm_f32le (the wav muxer defaults to pcm_s16le, which
rem  would throw away 8 bits before the effect even runs).
rem
rem  Examples:
rem    bmrun.cmd song.flac --drive 10 --bass 2 --treble 2 --preset jazz
rem    bmrun.cmd "D:\MV\a.mkv" -o a.wav --drive 9 --af "highpass=f=30"
rem    bmrun.cmd song.wav -o - --out-fmt f32le --drive 8
rem ===========================================================================
setlocal EnableDelayedExpansion

set "FF=D:\Program Files\ffmpeg\ffmpeg.exe"
set "CLI=%~dp0bassmax_cli.exe"

if not exist "%FF%" set "FF=ffmpeg.exe"
if not exist "%CLI%" (
  echo [bmrun] bassmax_cli.exe not found next to bmrun.cmd
  exit /b 1
)

set "IN="
set "OUT="
set "AF="
set "OPTS="
set "GOTBITS="

:parse
if "%~1"=="" goto ready
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help
if /i "%~1"=="-o" (
  set "OUT=%~2"
  set "OPTS=!OPTS! -o "%~2""
  shift
  shift
  goto parse
)
if /i "%~1"=="--out" (
  set "OUT=%~2"
  set "OPTS=!OPTS! --out "%~2""
  shift
  shift
  goto parse
)
if /i "%~1"=="--af" (
  set "AF=%~2"
  shift
  shift
  goto parse
)
if /i "%~1"=="--out-bits" set "GOTBITS=1"
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
  echo [bmrun] input not found: !IN!
  exit /b 3
)

rem ---- default output: <input>.bmx.wav (no -o means bassmax writes to stdout)
if not defined OUT (
  for %%F in ("!IN!") do set "OUT=%%~dpnF.bmx.wav"
  set "OPTS=!OPTS! -o "!OUT!""
)

rem ---- default 32 bit float: the result still has to go through an editor,
rem      so do not quantise here ----
if not defined GOTBITS set "OPTS=!OPTS! --out-bits f32"

rem ---- duration, parsed from ffmpeg itself (no ffprobe needed) ----
set "DUR="
set "DTMP=%TEMP%\bmrun_dur_%RANDOM%%RANDOM%.txt"
"%FF%" -hide_banner -i "!IN!" >nul 2>"!DTMP!"
for /f "tokens=2 delims=, " %%a in ('findstr /c:"Duration:" "!DTMP!" 2^>nul') do (
  if not defined DUR set "DUR=%%a"
)
if exist "!DTMP!" del /q "!DTMP!" >nul 2>nul
if defined DUR echo !DUR!| findstr /r "^[0-9][0-9]*:[0-9][0-9]*:[0-9]" >nul || set "DUR="
set "TOTAL="
if defined DUR set "TOTAL=--total !DUR!"

echo [bmrun] in   : !IN!
echo [bmrun] out  : !OUT!
if defined TOTAL (
  echo [bmrun] total: !DUR!
) else (
  echo [bmrun] total: unknown, progress shows elapsed time and speed only
)

set "AFOPT="
if defined AF set "AFOPT=-af "!AF!""

rem ---- decode (f32, no truncation) -> bassmax ----
"!FF!" -v error -nostdin -i "!IN!" -map 0:a:0 -vn !AFOPT! -f wav -c:a pcm_f32le - 2>nul | "!CLI!" - !TOTAL! !OPTS!
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo [bmrun] failed, rc=%RC% (no audio stream, or ffmpeg could not decode)
  exit /b 4
)
exit /b 0

:help
echo Usage: bmrun.cmd ^<input^> [-o ^<output^>] [--af "filter"] [bassmax options]
echo.
echo   -o ^<path^>        output file, "-" for stdout
echo                     default: ^<input name^>.bmx.wav
echo   --af "..."        extra ffmpeg audio filter, applied before BassMax
echo                     e.g. --af "highpass=f=30,loudnorm"
echo   --out-bits ^<n^>   16^|24^|32^|f32  (default f32: no quantisation before editing)
echo.
echo   --drive ^<0..10^>   bass/level stretch amount (0 = bypass)
echo   --bass  ^<dB^>      -12..12, 100Hz shelf
echo   --treble ^<dB^>     -12..12, 9kHz shelf
echo   --preset ^<name^>   standard^|classical^|club^|pop^|jazz^|rock
echo                     (Chinese preset names work too)
echo.
echo   Run bassmax_cli.exe --help for the full list.
exit /b 2
