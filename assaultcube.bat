@echo off
:: Request admin privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

:: Set working directory to the folder containing this bat file
cd /d "%~dp0"

start bin_win32\ac_client.exe "--home=?MYDOCUMENTS?\My Games\AssaultCube\v1.3" --init %1 %2 %3 %4 %5
