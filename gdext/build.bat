@echo off
rem Builds the game's GDExtension into ..\bin. Close Godot first: it locks the dll.
rem
rem   build.bat           builds in gdext\build
rem   build.bat C:\dir    builds there instead (a short path helps if MSBuild
rem                       complains about path length)
rem
rem Needs CMake 3.17+, Python 3 (godot-cpp generates its bindings with it) and
rem Visual Studio 2022 or its Build Tools with the C++ workload. Set
rem CMAKE_GENERATOR to use another generator, PYTHON to pick the interpreter.

setlocal
set "ROOT=%~dp0"
set "BUILD=%~1"
if "%BUILD%"=="" set "BUILD=%ROOT%build"

if not exist "%ROOT%godot-cpp\CMakeLists.txt" (
    echo godot-cpp is missing. Run:  git submodule update --init
    exit /b 1
)

if not defined CMAKE_GENERATOR set "CMAKE_GENERATOR=Visual Studio 17 2022"
set "ARCH="
echo %CMAKE_GENERATOR% | findstr /b /c:"Visual Studio" >nul && set "ARCH=-A x64"

set "PYARG="
if defined PYTHON set "PYARG=-DPython3_EXECUTABLE=%PYTHON%"

cmake -S "%ROOT%." -B "%BUILD%" %ARCH% %PYARG% || exit /b 1
cmake --build "%BUILD%" --config Release --parallel || exit /b 1
echo Built %ROOT%..\bin
