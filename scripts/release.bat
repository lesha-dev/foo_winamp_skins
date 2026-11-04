@echo off
rem Prepare release archives: out\foo_winamp_skins_x86.zip and out\foo_winamp_skins_x64.zip.
rem Each holds a foo_winamp_skins folder with the DLL, README.md, LICENSE and the third-party
rem license notices that must ship with the binaries (miniz, Webamp).
rem DLLs come from the Visual Studio Release builds (out\Win32\Release, out\x64\Release).
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\out"
if not exist "%OUT%" mkdir "%OUT%"

call :pack x86 "%ROOT%\out\Win32\Release\foo_winamp_skins.dll" || exit /b 1
call :pack x64 "%ROOT%\out\x64\Release\foo_winamp_skins.dll" || exit /b 1
echo Done.
exit /b 0

:pack
set "ARCH=%~1"
set "DLL=%~2"
if not exist "%DLL%" (
    echo Missing %DLL% - build the Release configuration for %ARCH% first.
    exit /b 1
)
set "STAGE=%TEMP%\foo_winamp_skins_release_%ARCH%"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\foo_winamp_skins"
copy /y "%DLL%" "%STAGE%\foo_winamp_skins\" >nul || exit /b 1
copy /y "%ROOT%\README.md" "%STAGE%\foo_winamp_skins\" >nul || exit /b 1
copy /y "%ROOT%\LICENSE" "%STAGE%\foo_winamp_skins\LICENSE.txt" >nul || exit /b 1
copy /y "%ROOT%\3rdParty\miniz\LICENSE" "%STAGE%\foo_winamp_skins\LICENSE-miniz.txt" >nul || exit /b 1
copy /y "%ROOT%\licenses\webamp-LICENSE.txt" "%STAGE%\foo_winamp_skins\LICENSE-webamp.txt" >nul || exit /b 1
set "ZIP=%OUT%\foo_winamp_skins_%ARCH%.zip"
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\foo_winamp_skins' -DestinationPath '%ZIP%'" || exit /b 1
rmdir /s /q "%STAGE%"
echo %ZIP%
exit /b 0
