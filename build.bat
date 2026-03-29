@echo off
setlocal

:: ============================================================================
:: AssaultCube Build Script
:: Builds the game client and/or server without opening Visual Studio.
:: Requires Visual Studio 2019+ (or Build Tools) to be installed.
::
:: Usage:
::   build.bat              Build Release client
::   build.bat debug        Build Debug client
::   build.bat release      Build Release client
::   build.bat server       Build Release server (Standalone)
::   build.bat server debug Build Debug server (Standalone Debug)
::   build.bat all          Build Release client + server
::   build.bat all debug    Build Debug client + server
::   build.bat clean        Clean all build artifacts
:: ============================================================================

set "TARGET=%~1"
set "MODE=%~2"

if "%TARGET%"=="" set "TARGET=client"

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

:: Handle clean
if /i "%TARGET%"=="clean" (
    echo Cleaning all configurations...
    "%MSBUILD%" "%SLN%" /t:Clean /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
    "%MSBUILD%" "%SLN%" /t:Clean /p:Configuration=Debug /p:Platform=Win32 /verbosity:minimal
    "%MSBUILD%" "%SLN%" /t:Clean /p:Configuration=Standalone /p:Platform=Win32 /verbosity:minimal
    "%MSBUILD%" "%SLN%" /t:Clean /p:Configuration="Standalone Debug" /p:Platform=Win32 /verbosity:minimal
    echo Clean complete.
    exit /b 0
)

:: Build client
if /i "%TARGET%"=="client" (
    if /i "%MODE%"=="debug" (
        echo Building client [Debug]...
        "%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=Win32 /verbosity:minimal
    ) else (
        echo Building client [Release]...
        "%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
    )
    goto :done
)

:: Build server
if /i "%TARGET%"=="server" (
    if /i "%MODE%"=="debug" (
        echo Building server [Standalone Debug]...
        "%MSBUILD%" "%SLN%" /p:Configuration="Standalone Debug" /p:Platform=Win32 /verbosity:minimal
    ) else (
        echo Building server [Standalone]...
        "%MSBUILD%" "%SLN%" /p:Configuration=Standalone /p:Platform=Win32 /verbosity:minimal
    )
    goto :done
)

:: Build all (client + server)
if /i "%TARGET%"=="all" (
    if /i "%MODE%"=="debug" (
        echo Building client [Debug]...
        "%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=Win32 /verbosity:minimal
        if errorlevel 1 exit /b 1
        echo Building server [Standalone Debug]...
        "%MSBUILD%" "%SLN%" /p:Configuration="Standalone Debug" /p:Platform=Win32 /verbosity:minimal
    ) else (
        echo Building client [Release]...
        "%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
        if errorlevel 1 exit /b 1
        echo Building server [Standalone]...
        "%MSBUILD%" "%SLN%" /p:Configuration=Standalone /p:Platform=Win32 /verbosity:minimal
    )
    goto :done
)

if /i "%TARGET%"=="release" (
    echo Building client [Release]...
    "%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
    goto :done
)

if /i "%TARGET%"=="debug" (
    echo Building client [Debug]...
    "%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=Win32 /verbosity:minimal
    goto :done
)

echo Unknown target: %TARGET%
echo Usage: build.bat [client^|server^|all^|clean^|debug^|release] [debug]
exit /b 1

:done
if errorlevel 1 (
    echo BUILD FAILED.
    exit /b 1
)
echo BUILD SUCCEEDED. Output: bin_win32\
exit /b 0
