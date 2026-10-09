@echo off
rem Draws the icon again (icons\makeicon.cpp) and writes qatest.ico and the
rem pictures into this folder. Run it after changing the drawing, then build.bat.
setlocal
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\6.11.0\msvc2022_64
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VCVARS=
if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    echo Visual Studio with the C++ tools was not found.
    exit /b 1
)
call "%VCVARS%" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%QT_DIR%\bin;%PATH%
cd /d "%~dp0.."

rem A build folder of its own: the program's cannot be configured before the icon is there.
cmake -S . -B build\icon -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_CXX_COMPILER=cl >nul
if errorlevel 1 exit /b 1
cmake --build build\icon --target MakeIcon
if errorlevel 1 exit /b 1
set QT_QPA_PLATFORM=offscreen
build\icon\MakeIcon.exe icons
exit /b %errorlevel%
