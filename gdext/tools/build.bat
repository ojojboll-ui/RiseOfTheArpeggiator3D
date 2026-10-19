@echo off
rem Builds and runs one offline test, e.g.  build.bat audiotest [outdir]
rem The audio headers are plain C++, so the tests need no SDL, no window and no pad.
rem
rem Finds the Visual Studio C++ tools with vswhere, which every Visual Studio
rem 2017+ installation has, unless cl is already on the PATH (a developer
rem command prompt).

setlocal
where cl >nul 2>nul && goto build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio not found: no vswhere.exe. Install Visual Studio or its Build Tools with C++.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio with the C++ x64 tools found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cd /d "%~dp0"
cl /nologo /std:c++17 /O2 /EHsc /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS %1.cpp /Fe:%1.exe /Fo:%1.obj && .\%1.exe %2
