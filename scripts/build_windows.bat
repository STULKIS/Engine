@echo off
rem Frontier Engine - one-command Windows build.
rem Requires CMake 3.20+ and a C++20 compiler (Visual Studio 2019/2022 or MinGW).
rem SDL2 is downloaded and built automatically, then copied next to the exe.
setlocal

set "ROOT=%~dp0.."
where cmake >nul 2>nul
if errorlevel 1 (
    echo [error] CMake was not found on PATH.
    echo         Install it from https://cmake.org/download/ and enable "Add to PATH".
    exit /b 1
)

echo === Configuring ===
cmake -S "%ROOT%" -B "%ROOT%\build" -DFRONTIER_FETCH_SDL2=ON
if errorlevel 1 (
    echo [error] Configure failed. See the messages above.
    exit /b 1
)

echo === Building ===
cmake --build "%ROOT%\build" --config Release
if errorlevel 1 (
    echo [error] Build failed. See the messages above.
    exit /b 1
)

echo.
echo Build complete. The game is in:
echo   "%ROOT%\build\Release\frontier.exe"   (Visual Studio generators)
echo   "%ROOT%\build\frontier.exe"           (MinGW / Ninja)
echo.
echo Double-click the exe, or start it from a console to see the log output.
