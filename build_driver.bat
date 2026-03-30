@echo off
setlocal

:: ============================================================================
:: Anti-Cheat Driver Build Script
:: Builds the phanticheat kernel driver (x64 only).
:: Requires Visual Studio 2019+ with the Windows Driver Kit (WDK) installed.
::
:: Usage:
::   build_driver.bat              Build Release driver (default)
::   build_driver.bat debug        Build Debug driver
::   build_driver.bat release      Build Release driver (explicit)
::   build_driver.bat clean        Clean all build artifacts
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

set "PROJ=%~dp0source\src\anticheat\driver\phanticheat.vcxproj"

if /i "%TARGET%"=="clean" (
    echo Cleaning driver build artifacts...
    "%MSBUILD%" "%PROJ%" /t:Clean /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
    "%MSBUILD%" "%PROJ%" /t:Clean /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
    echo Clean complete.
    exit /b 0
)

if /i "%TARGET%"=="debug" (
    echo Building driver [Debug^|x64]...
    "%MSBUILD%" "%PROJ%" /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
    goto :done
)

if /i "%TARGET%"=="release" (
    echo Building driver [Release^|x64]...
    "%MSBUILD%" "%PROJ%" /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
    goto :done
)

echo Unknown target: %TARGET%
echo Usage: build_driver.bat [release^|debug^|clean]
exit /b 1

:done
if errorlevel 1 (
    echo BUILD FAILED.
    exit /b 1
)
echo BUILD SUCCEEDED. Output: bin_win32\driver\
exit /b 0
