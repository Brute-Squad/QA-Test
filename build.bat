@echo off
rem Builds QA Test Tracker with MSVC, CMake and Ninja, and runs its tests.
rem
rem   build.bat            configure (the first time), build, test
rem   build.bat run        ... and start the program
rem
rem Wants Visual Studio 2022 or newer (its C++ tools) and Qt 6 for MSVC.
rem Where Qt is can be said beforehand:  set QT_DIR=C:\Qt\6.11.0\msvc2022_64
setlocal

if "%QT_DIR%"=="" set QT_DIR=C:\Qt\6.11.0\msvc2022_64
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo Qt was not found in %QT_DIR%. Set QT_DIR to the folder of Qt for MSVC, the one that has bin\qmake.exe.
    exit /b 1
)

rem The compiler's environment: the newest Visual Studio that has the C++ tools.
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VCVARS=
if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    echo Visual Studio with the C++ tools was not found.
    exit /b 1
)
rem (It may grumble about vswhere.exe on its way: that does no harm.)
call "%VCVARS%" >nul 2>&1

set PATH=C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%QT_DIR%\bin;%PATH%
cd /d "%~dp0"

if not exist build\CMakeCache.txt (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_CXX_COMPILER=cl
    if errorlevel 1 exit /b 1
)
cmake --build build
if errorlevel 1 exit /b 1

rem The tests, with Qt as it is installed.
rem (Not "if errorlevel 1": a program that crashed ends with a number below nothing.)
set QT_QPA_PLATFORM=offscreen
build\QATestTests.exe
if %errorlevel% neq 0 (
    echo The tests FAILED.
    exit /b 1
)
set QT_QPA_PLATFORM=

rem The program to use: dist\QATest.exe, with Qt's own files beside it, so that it
rem starts from the Explorer on a PC that has no Qt. (In a folder of its own: beside
rem the test program those files would keep it from finding the offscreen platform.)
if not exist dist mkdir dist
copy /y build\QATest.exe dist\QATest.exe >nul
if not exist dist\Qt6Core.dll (
    windeployqt --no-translations dist\QATest.exe >nul
    if errorlevel 1 (
        echo Qt's files could not be put beside dist\QATest.exe.
        exit /b 1
    )
)
rem Its configuration file, to say where the database is: made once, never written over.
dist\QATest.exe --write-config >nul
echo The program is dist\QATest.exe

if /i "%1"=="run" start "" dist\QATest.exe
exit /b 0
