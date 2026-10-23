@echo off
rem Derecho Engine - launcher that always leaves a readable window behind.
rem
rem Double-clicking derecho.exe directly works, but if start-up fails the
rem console window closes before the message can be read. This script keeps the
rem window open and tees the output to run-output.txt.
setlocal
cd /d "%~dp0"

rem Look next to this script first (CI artifact layout), then in the build tree
rem (repository layout, where this script lives in scripts\).
set "EXE=%~dp0derecho.exe"
if not exist "%EXE%" if exist "%~dp0build\Release\derecho.exe" set "EXE=%~dp0build\Release\derecho.exe"
if not exist "%EXE%" if exist "%~dp0build\derecho.exe" set "EXE=%~dp0build\derecho.exe"
if not exist "%EXE%" if exist "%~dp0..\build\Release\derecho.exe" set "EXE=%~dp0..\build\Release\derecho.exe"
if not exist "%EXE%" if exist "%~dp0..\build\derecho.exe" set "EXE=%~dp0..\build\derecho.exe"

if not exist "%EXE%" (
    echo [error] derecho.exe was not found next to this script.
    echo         Build it with scripts\build_windows.bat first.
    echo.
    pause
    exit /b 1
)

echo Launching "%EXE%" %*
echo.
"%EXE%" %*
set "CODE=%ERRORLEVEL%"
echo.
echo Exit code: %CODE%
if exist "%~dp0derecho.log" echo Log file: %~dp0derecho.log
if exist "%~dp0..\derecho.log" echo Log file: %~dp0..\derecho.log
if not "%CODE%"=="0" (
    echo.
    echo Try:  derecho.exe --selftest      ^(render one frame and verify it^)
    echo       derecho.exe --safe-mode      ^(no MSAA / HDR / bloom^)
    echo       derecho.exe --headless=60    ^(no window, no GPU^)
)
echo.
pause
