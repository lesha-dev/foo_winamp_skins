@echo off
rem Prepare out\foo_winamp_skins-<ver>.fb2k-component: x86 DLL at the root, x64 DLL in x64\;
rem README.md, LICENSE and the third-party license notices (miniz, Webamp) in both folders.
rem Rebuilds Release x86 and x64 with MSBuild first (found via vswhere), then packs
rem out\Win32\Release and out\x64\Release.
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\out"
if not exist "%OUT%" mkdir "%OUT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found - is Visual Studio installed?
    exit /b 1
)
set "MSBUILD="
for /f "usebackq delims=" %%M in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%M"
if not defined MSBUILD (
    echo MSBuild.exe not found.
    exit /b 1
)
echo Using %MSBUILD%

call :build x86 || exit /b 1
call :build x64 || exit /b 1
call :pack || exit /b 1
echo Done.
exit /b 0

:build
echo Rebuilding Release^|%~1...
"%MSBUILD%" "%ROOT%\foo_winamp_skins.sln" -t:Rebuild -p:Configuration=Release -p:Platform=%~1 -m -nologo -v:minimal
if errorlevel 1 (
    echo Build failed for %~1.
    exit /b 1
)
exit /b 0

:pack
set "DLL86=%ROOT%\out\Win32\Release\foo_winamp_skins.dll"
set "DLL64=%ROOT%\out\x64\Release\foo_winamp_skins.dll"
for %%D in ("%DLL86%" "%DLL64%") do if not exist "%%~D" (
    echo Missing %%~D
    exit /b 1
)
set "VER="
for /f "tokens=3" %%V in ('findstr /c:"define COMPONENT_VERSION " "%ROOT%\src\version.h"') do set "VER=%%~V"
if not defined VER (
    echo COMPONENT_VERSION not found in src\version.h.
    exit /b 1
)
set "STAGE=%TEMP%\foo_winamp_skins_release"
set "PKG=%STAGE%\foo_winamp_skins"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%PKG%\x64"
copy /y "%DLL86%" "%PKG%\" >nul || exit /b 1
copy /y "%DLL64%" "%PKG%\x64\" >nul || exit /b 1
for %%P in ("%PKG%" "%PKG%\x64") do (
    copy /y "%ROOT%\README.md" "%%~P\" >nul || exit /b 1
    copy /y "%ROOT%\LICENSE" "%%~P\LICENSE.txt" >nul || exit /b 1
    copy /y "%ROOT%\3rdParty\miniz\LICENSE" "%%~P\LICENSE-miniz.txt" >nul || exit /b 1
    copy /y "%ROOT%\licenses\webamp-LICENSE.txt" "%%~P\LICENSE-webamp.txt" >nul || exit /b 1
)
rem Windows tar (bsdtar) writes zip entries with "/" separators; Compress-Archive in
rem PowerShell 5.1 uses "\", so foobar2000 would not see the x64 folder. Files go at the archive root.
set "COMPONENT=%OUT%\foo_winamp_skins-%VER%.fb2k-component"
if exist "%COMPONENT%" del /q "%COMPONENT%"
tar -c --format zip -f "%COMPONENT%" -C "%PKG%" foo_winamp_skins.dll README.md LICENSE.txt LICENSE-miniz.txt LICENSE-webamp.txt x64 || exit /b 1
rmdir /s /q "%STAGE%"
echo %COMPONENT%
exit /b 0
