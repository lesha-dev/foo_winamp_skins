@echo off
rem Close the portable foobar2000, copy the newest foo_winamp_skins build into it, start it again.
rem Usage: redeploy.bat [x64|x86]   (default x64)
rem Newest DLL wins among: unix\build-<arch>, out\<plat>\Debug, out\<plat>\Release.
setlocal
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"
for %%I in ("%~dp0..") do set "REPO=%%~fI"

if /I "%ARCH%"=="x64" (
    set "PLAT=x64"
    set "FB2K=%REPO%\..\foobar2000_x64"
    set "COMP=user-components-x64"
) else if /I "%ARCH%"=="x86" (
    set "PLAT=Win32"
    set "FB2K=%REPO%\..\foobar2000_x32"
    set "COMP=user-components"
) else (
    echo Unknown arch "%ARCH%", use x64 or x86.
    exit /b 1
)
for %%I in ("%FB2K%") do set "FB2K=%%~fI"
if not exist "%FB2K%\foobar2000.exe" (
    echo foobar2000 not found: %FB2K%
    exit /b 1
)

set "SRC="
for /f "usebackq delims=" %%F in (`powershell -NoProfile -Command "$c = 'unix\build-%ARCH%','out\%PLAT%\Debug','out\%PLAT%\Release' | ForEach-Object { Join-Path '%REPO%' (Join-Path $_ 'foo_winamp_skins.dll') } | Where-Object { Test-Path $_ } | Get-Item | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($c) { $c.FullName }"`) do set "SRC=%%F"
if not defined SRC (
    echo No foo_winamp_skins.dll build found for %ARCH%.
    exit /b 1
)
echo Using %SRC%

rem Close only the foobar2000 from this install: polite close first (saves config), force after 10 s.
powershell -NoProfile -Command "$exe = '%FB2K%\foobar2000.exe'; $p = Get-Process foobar2000 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }; if ($p) { Write-Host 'Closing foobar2000...'; $p | ForEach-Object { [void]$_.CloseMainWindow() }; $p | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue; $p | Where-Object { -not $_.HasExited } | Stop-Process -Force }"

set "DST=%FB2K%\profile\%COMP%\foo_winamp_skins"
if not exist "%DST%" mkdir "%DST%"
copy /Y "%SRC%" "%DST%\" >nul || (echo Copy failed. & exit /b 1)
for %%P in ("%SRC%") do if exist "%%~dpnP.pdb" copy /Y "%%~dpnP.pdb" "%DST%\" >nul
echo Copied to %DST%

rem Explicit Normal window style: a desktop shortcut's "Run: Minimized" would otherwise be inherited.
powershell -NoProfile -Command "Start-Process -FilePath '%FB2K%\foobar2000.exe' -WorkingDirectory '%FB2K%' -WindowStyle Normal"
endlocal
