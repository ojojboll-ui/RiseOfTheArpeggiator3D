@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /std:c++17 /Zi /Od /EHsc /fsanitize=address /utf-8 /D_CRT_SECURE_NO_WARNINGS audiotest.cpp /Fe:audiotest_asan.exe /Fo:audiotest_asan.obj >nul && .\audiotest_asan.exe %1
