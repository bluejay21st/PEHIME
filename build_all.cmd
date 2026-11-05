@echo off
rem ===================================================================
rem PEIM build script (project root)
rem
rem Every build:
rem   1) regenerate the CN dictionary with tools\words2dict.py
rem      (requires Python 3.8+ with jieba/pypinyin/wordfreq)
rem   2) compile build\PEIMH.EXE
rem   3) compile build\ptest.exe
rem
rem If tools\w64devkit is missing, tools\get_w64devkit.ps1 is run
rem automatically to download it.
rem ===================================================================
setlocal
cd /d "%~dp0"

set "TC=%~dp0tools\w64devkit\w64devkit\bin"
set "CC=%TC%\x86_64-w64-mingw32-gcc.exe"
set "TMP=%~dp0.tmp"
set "TEMP=%TMP%"
if not exist "%TMP%" mkdir "%TMP%"
if not exist "build" mkdir "build"

rem ---------- 1) Python 3.8+ ----------
python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python 3.8+ not found in PATH.
  echo.
  echo Install Python 3 first: https://www.python.org/downloads/
  echo Enable "Add python.exe to PATH" during installation, then reopen the terminal.
  echo Then install dictionary dependencies:
  echo   python -m pip install jieba pypinyin wordfreq
  exit /b 1
)

rem ---------- 2) compiler (auto download if missing) ----------
if not exist "%CC%" (
  echo [INFO] compiler not found: "%CC%"
  echo [INFO] running tools\get_w64devkit.ps1 ...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\get_w64devkit.ps1"
  if errorlevel 1 (
    echo [ERROR] failed to download/extract w64devkit.
    exit /b 1
  )
)
if not exist "%CC%" (
  echo [ERROR] compiler still not found: "%CC%"
  exit /b 1
)

rem ---------- 3) dictionary (always regenerate) ----------
echo [1/3] build dictionary with tools\words2dict.py
python "%~dp0tools\words2dict.py"
if errorlevel 1 (
  echo [ERROR] words2dict.py failed.
  echo         Check: python -m pip install jieba pypinyin wordfreq
  exit /b 1
)

rem ---------- 4) main program ----------
echo [2/3] build PEIMH.EXE ^(hook IME^)
"%CC%" -O2 -Wall -Wextra -std=c99 -DUNICODE -D_UNICODE -Isrc/core -Isrc/ui -Isrc/common ^
  -mwindows -o build\PEIMH.EXE ^
  src\hook\hook_main.c src\core\peim_dict.c src\core\peim_pinyin.c ^
  src\ui\peim_candwin.c src\ui\peim_tray.c src\common\peim_log.c src\common\peim_config.c ^
  -luser32 -lgdi32 -lshell32 -ladvapi32 -static -static-libgcc -s
if errorlevel 1 (
  echo [ERROR] PEIMH build failed.
  exit /b 1
)

rem ---------- 5) test program ----------
echo [3/3] build ptest.exe ^(pinyin core test^)
"%CC%" -O2 -Wall -Wextra -std=c99 -DUNICODE -D_UNICODE -Isrc/core ^
  -o build\ptest.exe src\core\peim_dict.c src\core\peim_pinyin.c tools\ptest.c ^
  -static -static-libgcc -s
if errorlevel 1 (
  echo [ERROR] ptest build failed.
  exit /b 1
)

echo.
echo BUILD OK
dir /b build
exit /b 0
