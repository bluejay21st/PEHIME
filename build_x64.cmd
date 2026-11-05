@echo off
rem ===================================================================
rem PEHIME 64-bit executable build script (x64)
rem
rem Output: build\x64\PEHIME.EXE, build\x64\ptest.exe
rem Dictionary is NOT built here; run build_words.cmd first
rem (it copies pehime.dict into build\x64 and build\x86).
rem ===================================================================
setlocal
cd /d "%~dp0"

set "TC=%~dp0tools\w64devkit\w64devkit\bin"
set "CC=%TC%\x86_64-w64-mingw32-gcc.exe"
set "TMP=%~dp0.tmp"
set "TEMP=%TMP%"
if not exist "%TMP%" mkdir "%TMP%"
if not exist "build" mkdir "build"
if not exist "build\x64" mkdir "build\x64"

rem ---------- 1) compiler (auto download if missing) ----------
if not exist "%CC%" (
  echo [INFO] 64-bit compiler not found: "%CC%"
  echo [INFO] running tools\get_w64devkit.ps1 ...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\get_w64devkit.ps1"
  if errorlevel 1 (
    echo [ERROR] failed to download/extract w64devkit.
    exit /b 1
  )
)
if not exist "%CC%" (
  echo [ERROR] 64-bit compiler still not found: "%CC%"
  exit /b 1
)

rem ---------- 2) main program ----------
echo [1/2] build build\x64\PEHIME.EXE ^(64-bit hook IME^)
"%CC%" -O2 -Wall -Wextra -std=c99 -DUNICODE -D_UNICODE -Isrc/core -Isrc/ui -Isrc/common ^
  -mwindows -o build\x64\PEHIME.EXE ^
  src\hook\hook_main.c src\core\pehime_dict.c src\core\pehime_pinyin.c ^
  src\ui\pehime_candwin.c src\ui\pehime_tray.c src\common\pehime_log.c src\common\pehime_config.c ^
  -luser32 -lgdi32 -lshell32 -ladvapi32 -static -static-libgcc -s
if errorlevel 1 (
  echo [ERROR] PEHIME 64-bit build failed.
  exit /b 1
)

rem ---------- 3) test program ----------
echo [2/2] build build\x64\ptest.exe ^(64-bit pinyin core test^)
"%CC%" -O2 -Wall -Wextra -std=c99 -DUNICODE -D_UNICODE -Isrc/core ^
  -o build\x64\ptest.exe src\core\pehime_dict.c src\core\pehime_pinyin.c tools\ptest.c ^
  -static -static-libgcc -s
if errorlevel 1 (
  echo [ERROR] ptest 64-bit build failed.
  exit /b 1
)

echo.
echo BUILD OK ^(x64^)
dir /b build\x64
exit /b 0
