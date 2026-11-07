@echo off
setlocal
cd /d "%~dp0"
if defined W64DEVKIT set "PATH=%W64DEVKIT%\bin;%PATH%"
if not defined W64DEVKIT if exist "C:\w64devkit\bin\g++.exe" set "PATH=C:\w64devkit\bin;%PATH%"
where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ not found. Add w64devkit/bin to PATH or set W64DEVKIT.
    exit /b 1
)
make %*
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
if exist "%cd%\build\bluewhale.exe" echo built: %cd%\build\bluewhale.exe
