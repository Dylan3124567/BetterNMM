@echo off
setlocal
rem Builds BetterNMM (only what changed), bundles it with the Qt runtime in .\install and starts it.
rem The bundled copy runs without MSYS2 on PATH, which nxm:// links from the browser need.
rem Arguments go to BetterNMM, for example:  start.bat --launch "Skyrim Special Edition"

set "ROOT=%~dp0"
if not defined MSYS2 set "MSYS2=C:\msys64"
set "PATH=%MSYS2%\mingw64\bin;%PATH%"
set "BUILD=%ROOT%build-release"
set "INSTALL=%ROOT%install"

rem a running BetterNMM keeps its exe locked: hand the arguments to it instead of rebuilding
tasklist /fi "imagename eq betternmm.exe" 2>nul | find /i "betternmm.exe" >nul
if not errorlevel 1 if exist "%INSTALL%\betternmm.exe" goto :start

if not exist "%MSYS2%\mingw64\bin\cmake.exe" (
    echo MSYS2 with its MinGW64 toolchain was not found in %MSYS2%.
    echo Install MSYS2 from https://www.msys2.org, then run this in its "MSYS2 MINGW64" shell:
    echo   pacman -S mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-libarchive mingw-w64-x86_64-extra-cmake-modules mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc
    echo If MSYS2 lives elsewhere, run:  set MSYS2=D:\path\to\msys64  before this script.
    goto :failed
)

if not exist "%BUILD%\build.ninja" (
    echo First build: this takes a few minutes.
    cmake -S "%ROOT%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH="%MSYS2%\mingw64" -Wno-dev
    if errorlevel 1 goto :failed
)

echo Building BetterNMM...
cmake --build "%BUILD%" --target BetterNMM
if errorlevel 1 (
    rem many compilers at once can run a PC out of memory; two at a time is slow but fits
    echo Trying again with fewer parallel jobs...
    cmake --build "%BUILD%" --target BetterNMM -j 2
    if errorlevel 1 goto :failed
)

echo Bundling it into %INSTALL%...
cmake --install "%BUILD%" --prefix "%INSTALL%" > "%BUILD%\install.log" 2>&1
if errorlevel 1 (
    type "%BUILD%\install.log"
    goto :failed
)

:start
start "" "%INSTALL%\betternmm.exe" %*
exit /b 0

:failed
echo.
echo BetterNMM could not be built; the messages above say why.
pause
exit /b 1
