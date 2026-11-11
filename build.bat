@echo off
setlocal EnableExtensions

rem ===========================================================================
rem  build.bat [FOOBAR2000_SDK_ROOT] [WTL_ROOT]
rem
rem  Builds the SDK libraries and the component (Release | x64, MSVC v143
rem  toolset) and packages foo_out_wasapi_advanced.fb2k-component.
rem
rem  The two paths can be passed as arguments or taken from the FB2K_SDK and
rem  WTL_ROOT environment variables. Example:
rem    build.bat C:\dev\foobar-SDK-2026-10-01 C:\dev\WTL10_01_Release
rem ===========================================================================

set "SCRIPT_DIR=%~dp0"
set "SOLUTION=%SCRIPT_DIR%foo_out_wasapi_advanced.slnx"
set "COMPONENT_NAME=foo_out_wasapi_advanced"
set "TOOLSET=v143"

if /I "%~1"=="/?" goto :usage
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage

set "SDK_DIR=%~1"
set "WTL_DIR=%~2"
if not defined SDK_DIR set "SDK_DIR=%FB2K_SDK%"
if not defined WTL_DIR set "WTL_DIR=%WTL_ROOT%"

if not defined SDK_DIR goto :missing_args
if not defined WTL_DIR goto :missing_args

for %%I in ("%SDK_DIR%") do set "SDK_DIR=%%~fI"
for %%I in ("%WTL_DIR%") do set "WTL_DIR=%%~fI"
if "%SDK_DIR:~-1%"=="\" set "SDK_DIR=%SDK_DIR:~0,-1%"
if "%WTL_DIR:~-1%"=="\" set "WTL_DIR=%WTL_DIR:~0,-1%"

if not exist "%SOLUTION%" (
    echo ERROR: Solution not found: "%SOLUTION%"
    exit /b 1
)
if not exist "%SDK_DIR%\pfc\pfc.vcxproj" (
    echo ERROR: foobar2000 SDK not found in "%SDK_DIR%" - expected pfc\pfc.vcxproj there.
    exit /b 1
)
if not exist "%WTL_DIR%\Include\atlapp.h" (
    echo ERROR: WTL not found in "%WTL_DIR%" - expected Include\atlapp.h there.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio vswhere.exe not found: "%VSWHERE%"
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
    echo ERROR: No Visual Studio installation with MSBuild was found.
    exit /b 1
)

set "MSBUILD=%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo ERROR: MSBuild not found: "%MSBUILD%"
    exit /b 1
)

rem ATL headers: prefer the ones that belong to the v143 toolset (14.3x / 14.4x). If the v143 ATL
rem component is not installed, fall back to the newest ATL found in any installed toolset.
set "ATL_INCLUDE="
set "ATL_FALLBACK="
for /d %%I in ("%VSINSTALL%\VC\Tools\MSVC\14.4*") do if exist "%%I\atlmfc\include\atlbase.h" set "ATL_INCLUDE=%%I\atlmfc\include"
if not defined ATL_INCLUDE for /d %%I in ("%VSINSTALL%\VC\Tools\MSVC\14.3*") do if exist "%%I\atlmfc\include\atlbase.h" set "ATL_INCLUDE=%%I\atlmfc\include"
if not defined ATL_INCLUDE for /d %%I in ("%VSINSTALL%\VC\Tools\MSVC\*") do if exist "%%I\atlmfc\include\atlbase.h" set "ATL_INCLUDE=%%I\atlmfc\include" & set "ATL_FALLBACK=1"
if not defined ATL_INCLUDE goto :no_atl
if defined ATL_FALLBACK echo WARNING: No ATL for the v143 toolset is installed. Using newer ATL headers from "%ATL_INCLUDE%"
if defined ATL_FALLBACK echo          For a fully matching setup install "C++ ATL for latest v143 build tools" in the Visual Studio Installer.
echo Using ATL headers: "%ATL_INCLUDE%"

call "%SCRIPT_DIR%clean.bat"
if errorlevel 1 goto :failed

call :build_sdk "%SDK_DIR%\pfc\pfc.vcxproj"
if errorlevel 1 goto :failed
call :build_sdk "%SDK_DIR%\foobar2000\SDK\foobar2000_SDK.vcxproj"
if errorlevel 1 goto :failed
call :build_sdk "%SDK_DIR%\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj"
if errorlevel 1 goto :failed

rem The helpers library needs ATL and WTL headers.
set "PREVIOUS_CL=%CL%"
set "CL=%CL% /I"%WTL_DIR%\Include" /I"%ATL_INCLUDE%""
call :build_sdk "%SDK_DIR%\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj"
set "BUILD_RESULT=%ERRORLEVEL%"
set "CL=%PREVIOUS_CL%"
if not "%BUILD_RESULT%"=="0" goto :failed

echo Building Release x64 component...
call "%MSBUILD%" "%SOLUTION%" /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=%TOOLSET% "/p:Fb2kSdkRoot=%SDK_DIR%" "/p:WtlRoot=%WTL_DIR%" "/p:AtlInclude=%ATL_INCLUDE%" /m:1 /nologo
if errorlevel 1 goto :failed

set "DLL=%SCRIPT_DIR%x64\Release\%COMPONENT_NAME%.dll"
set "PACKAGE=%SCRIPT_DIR%%COMPONENT_NAME%.fb2k-component"
if not exist "%DLL%" (
    echo ERROR: Expected component DLL was not produced: "%DLL%"
    exit /b 1
)

echo Packaging "%DLL%"
powershell.exe -NoProfile -Command "$ErrorActionPreference = 'Stop'; try { Compress-Archive -LiteralPath '%DLL%' -DestinationPath '%PACKAGE%.zip' -Force; Move-Item -LiteralPath '%PACKAGE%.zip' -Destination '%PACKAGE%' -Force } catch { Write-Error $_; exit 1 }"
if errorlevel 1 goto :failed

echo Created installable component: "%PACKAGE%"
exit /b 0

:build_sdk
echo Building SDK library: %~nx1
call "%MSBUILD%" "%~1" /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=%TOOLSET% /m:1 /nologo
exit /b %ERRORLEVEL%

:no_atl
echo ERROR: No ATL headers ^(atlmfc\include\atlbase.h^) found under "%VSINSTALL%".
echo Install the "C++ ATL for latest v143 build tools" component in the Visual Studio Installer.
exit /b 1

:missing_args
echo ERROR: SDK and/or WTL path not given.
echo.
:usage
echo Usage: build.bat [FOOBAR2000_SDK_ROOT] [WTL_ROOT]
echo.
echo   FOOBAR2000_SDK_ROOT  folder that contains pfc\ and foobar2000\  - or set FB2K_SDK
echo   WTL_ROOT             folder that contains Include\atlapp.h        - or set WTL_ROOT
echo.
echo Example: build.bat C:\dev\foobar-SDK-2026-10-01 C:\dev\WTL10_01_Release
exit /b 1

:failed
echo.
echo Build or packaging failed.
echo If MSBuild reported MSB8020 the MSVC v143 build tools component is not installed in Visual Studio.
exit /b 1
