@echo off
setlocal

:: ============================================================================
:: AssaultCube Client Build Script
:: Requires Visual Studio 2019+ (or Build Tools) to be installed.
::
:: Usage:
::   build_client.bat              Build Release client (default)
::   build_client.bat debug        Build Debug client
::   build_client.bat release      Build Release client (explicit)
::   build_client.bat clean        Clean all build artifacts
:: ============================================================================

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=release"

:: Resolve MSBuild via vswhere (works for VS 2017+)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere not found. Install Visual Studio 2019+ or Build Tools.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo ERROR: MSBuild not found. Install Visual Studio 2019+ or Build Tools.
    exit /b 1
)

echo Using MSBuild: %MSBUILD%

set "SLN=%~dp0source\vcpp\assaultcube.sln"

if /i "%TARGET%"=="clean" (
    echo Cleaning client build artifacts...
    "%MSBUILD%" "%SLN%" /t:Clean /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
    "%MSBUILD%" "%SLN%" /t:Clean /p:Configuration=Debug /p:Platform=Win32 /verbosity:minimal
    echo Clean complete.
    exit /b 0
)

if /i "%TARGET%"=="debug" (
    echo Building client [Debug]...
    "%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=Win32 /verbosity:minimal
    goto :done
)

if /i "%TARGET%"=="release" (
    echo Building client [Release]...
    "%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
    goto :done
)

echo Unknown target: %TARGET%
echo Usage: build_client.bat [release^|debug^|clean]
exit /b 1

:done
if errorlevel 1 (
    echo BUILD FAILED.
    exit /b 1
)
echo BUILD SUCCEEDED. Output: bin_win32\
exit /b 0
