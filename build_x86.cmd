@echo off
rem ===================================================================
rem PEHIME 32-bit build script (x86)
rem
rem Output:
rem   build\x86\PEHIME.EXE    32-bit main program
rem   build\x86\ptest.exe     32-bit pinyin core test
rem   build\x86\pehime.dict   copied from build\ (if present)
rem   build\x86\pehime.ini    copied from build\ (if present)
rem
rem Requires build\pehime.dict; run build_all.cmd first.
rem If tools\w64devkit is missing, tools\get_w64devkit.ps1 is run
rem automatically to download it.
rem ===================================================================
setlocal
cd /d "%~dp0"

set "TC=%~dp0tools\w64devkit\w64devkit\bin"
set "CC=%TC%\i686-w64-mingw32-gcc.exe"
set "TMP=%~dp0.tmp"
set "TEMP=%TMP%"
if not exist "%TMP%" mkdir "%TMP%"
if not exist "build" mkdir "build"
if not exist "build\x86" mkdir "build\x86"

rem ---------- 1) 32-bit compiler (auto download if missing) ----------
if not exist "%CC%" (
  echo [INFO] 32-bit compiler not found: "%CC%"
  echo [INFO] running tools\get_w64devkit.ps1 ...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\get_w64devkit.ps1"
  if errorlevel 1 (
    echo [ERROR] failed to download/extract w64devkit.
    exit /b 1
  )
)
if not exist "%CC%" (
  echo [ERROR] 32-bit compiler still not found: "%CC%"
  exit /b 1
)

rem ---------- 2) dictionary (must exist) ----------
if not exist "build\pehime.dict" (
  echo [ERROR] build\pehime.dict not found.
  echo         Run build_all.cmd first to generate the dictionary.
  exit /b 1
)

rem ---------- 3) main program ----------
echo [1/2] build build\x86\PEHIME.EXE ^(32-bit hook IME^)
"%CC%" -O2 -Wall -Wextra -std=c99 -DUNICODE -D_UNICODE -Isrc/core -Isrc/ui -Isrc/common ^
  -mwindows -o build\x86\PEHIME.EXE ^
  src\hook\hook_main.c src\core\pehime_dict.c src\core\pehime_pinyin.c ^
  src\ui\pehime_candwin.c src\ui\pehime_tray.c src\common\pehime_log.c src\common\pehime_config.c ^
  -luser32 -lgdi32 -lshell32 -ladvapi32 -static -static-libgcc -s
if errorlevel 1 (
  echo [ERROR] PEHIME 32-bit build failed.
  exit /b 1
)

rem ---------- 4) test program ----------
echo [2/2] build build\x86\ptest.exe ^(32-bit pinyin core test^)
"%CC%" -O2 -Wall -Wextra -std=c99 -DUNICODE -D_UNICODE -Isrc/core ^
  -o build\x86\ptest.exe src\core\pehime_dict.c src\core\pehime_pinyin.c tools\ptest.c ^
  -static -static-libgcc -s
if errorlevel 1 (
  echo [ERROR] ptest 32-bit build failed.
  exit /b 1
)

rem ---------- 5) runtime files ----------
if exist "build\pehime.dict" copy /y "build\pehime.dict" "build\x86\pehime.dict" >nul
if exist "build\pehime.ini" copy /y "build\pehime.ini" "build\x86\pehime.ini" >nul

echo.
echo BUILD OK ^(x86^)
dir /b build\x86
exit /b 0
