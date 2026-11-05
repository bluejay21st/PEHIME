@echo off
rem ===================================================================
rem PEHIME dictionary build script
rem
rem Steps:
rem   1) check Python 3.8+ (missing -> error, wait for key, exit)
rem   2) check jieba / pypinyin / wordfreq; if missing, prompt and
rem      install them with pip
rem   3) run tools\words2dict.py
rem   4) copy build\pehime.dict into build\x64\ and build\x86\
rem
rem Output layout:
rem   build\x64\pehime.dict
rem   build\x86\pehime.dict
rem   build\pehime_cn_dump.txt  (debug dump, not needed at runtime)
rem ===================================================================
setlocal
cd /d "%~dp0"

set "TMP=%~dp0.tmp"
set "TEMP=%TMP%"
if not exist "%TMP%" mkdir "%TMP%"
if not exist "build" mkdir "build"
if not exist "build\x64" mkdir "build\x64"
if not exist "build\x86" mkdir "build\x86"

rem ---------- 1) Python 3.8+ ----------
python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>nul
if errorlevel 1 goto no_python

rem ---------- 2) dictionary dependencies ----------
python -c "import jieba, pypinyin, wordfreq" >nul 2>nul
if not errorlevel 1 goto deps_ok

echo [INFO] Dictionary dependencies missing: jieba / pypinyin / wordfreq
echo [INFO] Press any key to install them now ^(python -m pip install^)...
pause >nul
python -m pip install jieba pypinyin wordfreq
if errorlevel 1 goto pip_failed
python -c "import jieba, pypinyin, wordfreq" >nul 2>nul
if errorlevel 1 goto dep_recheck_failed

:deps_ok
rem ---------- 3) dictionary ----------
echo [1/2] build dictionary with tools\words2dict.py
python "%~dp0tools\words2dict.py"
if errorlevel 1 goto words_failed
if not exist "build\pehime.dict" goto dict_missing

rem ---------- 4) copy to architecture directories ----------
echo [2/2] copy dictionary to build\x64 and build\x86
copy /y "build\pehime.dict" "build\x64\pehime.dict" >nul
if errorlevel 1 goto copy_x64_failed
copy /y "build\pehime.dict" "build\x86\pehime.dict" >nul
if errorlevel 1 goto copy_x86_failed
del /q "build\pehime.dict" >nul 2>nul

echo.
echo BUILD WORDS OK
echo   build\x64\pehime.dict
echo   build\x86\pehime.dict
echo   build\pehime_cn_dump.txt  ^(debug only^)
exit /b 0

rem ---------- error paths ----------
:no_python
echo [ERROR] Python 3.8+ not found or too old.
echo.
echo Install Python 3.8+ first: https://www.python.org/downloads/
echo Remember to enable "Add python.exe to PATH".
echo.
pause
exit /b 1

:pip_failed
echo [ERROR] pip install failed. Check your network and Python/pip setup.
pause
exit /b 1

:dep_recheck_failed
echo [ERROR] dependencies are still missing after installation.
pause
exit /b 1

:words_failed
echo [ERROR] words2dict.py failed.
pause
exit /b 1

:dict_missing
echo [ERROR] build\pehime.dict was not generated.
pause
exit /b 1

:copy_x64_failed
echo [ERROR] failed to copy dictionary to build\x64
pause
exit /b 1

:copy_x86_failed
echo [ERROR] failed to copy dictionary to build\x86
pause
exit /b 1
