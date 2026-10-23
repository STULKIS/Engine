@echo off
rem Frontier Engine - launcher that always leaves a readable window behind.
rem
rem Double-clicking frontier.exe directly works, but if start-up fails the
rem console window closes before the message can be read. This script keeps the
rem window open and tees the output to run-output.txt.
setlocal
cd /d "%~dp0"

rem Look next to this script first (CI artifact layout), then in the build tree
rem (repository layout, where this script lives in scripts\).
set "EXE=%~dp0frontier.exe"
if not exist "%EXE%" if exist "%~dp0build\Release\frontier.exe" set "EXE=%~dp0build\Release\frontier.exe"
if not exist "%EXE%" if exist "%~dp0build\frontier.exe" set "EXE=%~dp0build\frontier.exe"
if not exist "%EXE%" if exist "%~dp0..\build\Release\frontier.exe" set "EXE=%~dp0..\build\Release\frontier.exe"
if not exist "%EXE%" if exist "%~dp0..\build\frontier.exe" set "EXE=%~dp0..\build\frontier.exe"

if not exist "%EXE%" (
    echo [error] frontier.exe was not found next to this script.
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
if exist "%~dp0frontier.log" echo Log file: %~dp0frontier.log
if exist "%~dp0..\frontier.log" echo Log file: %~dp0..\frontier.log
if not "%CODE%"=="0" (
    echo.
    echo Try:  frontier.exe --selftest      ^(render one frame and verify it^)
    echo       frontier.exe --safe-mode      ^(no MSAA / HDR / bloom^)
    echo       frontier.exe --headless=60    ^(no window, no GPU^)
)
echo.
pause
