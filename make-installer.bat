@echo off
setlocal
rem Builds BetterNMM and packs it into one setup program: dist\BetterNMM-Setup-<version>.exe
rem That file installs, updates and uninstalls BetterNMM (see TESTING.md, "Setup").

set "ROOT=%~dp0"
if not defined MSYS2 set "MSYS2=C:\msys64"
set "PATH=%MSYS2%\mingw64\bin;%PATH%"
set "BUILD=%ROOT%build-release"
set "DIST=%ROOT%dist"

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

rem a setup left from an older version would be taken for this one (*.*.* leaves out betternmm-setup-stub.exe)
del /q "%BUILD%\BetterNMM-Setup-*.*.*.exe" 2>nul
echo Building BetterNMM and its setup...
cmake --build "%BUILD%" --target setup
if errorlevel 1 (
    rem many compilers at once can run a PC out of memory; two at a time is slow but fits
    echo Trying again with fewer parallel jobs...
    cmake --build "%BUILD%" --target setup -j 2
    if errorlevel 1 goto :failed
)

if not exist "%DIST%" mkdir "%DIST%"
set "BUILT="
for %%F in ("%BUILD%\BetterNMM-Setup-*.*.*.exe") do set "BUILT=%%F"
if not defined BUILT (
    echo The build finished without making a BetterNMM-Setup program.
    goto :failed
)
copy /y "%BUILT%" "%DIST%\" >nul
if errorlevel 1 goto :failed
for %%F in ("%BUILT%") do echo Done: %DIST%\%%~nxF
exit /b 0

:failed
echo.
echo The setup program could not be built; the messages above say why.
pause
exit /b 1
