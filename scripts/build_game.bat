@echo off
rem Builds a project's game module (target MagnumGame) in <project>\Saved\GameBuild. The editor runs this on every source change.
rem Usage: build_game.bat <project folder>
rem Finds Visual Studio with vswhere, so it works from any shell (no developer prompt needed).
setlocal
set "ENGINE=%~dp0.."
set "PROJECT=%~1"
if "%PROJECT%"=="" (
    echo build_game.bat: missing the project folder
    exit /b 2
)

set "VSROOT="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CMAKEDIR=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CMAKEDIR%\CMake\bin;%CMAKEDIR%\Ninja;%PATH%"

set "BUILD=%PROJECT%\Saved\GameBuild"
if not exist "%BUILD%\build.ninja" (
    cmake -S "%PROJECT%\Game" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DMAGNUM_ENGINE_DIR=%ENGINE%" || exit /b 1
)
cmake --build "%BUILD%" --target MagnumGame
exit /b %errorlevel%
