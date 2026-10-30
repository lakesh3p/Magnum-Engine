@echo off
rem Starts the Magnum editor. Double-click it, or run it from a shell.
rem   Magnum.bat                     opens the last project (MagnumDemo the first time)
rem   Magnum.bat --project <folder>  opens a project folder; a missing or empty folder becomes a new project
rem Uses the Release build when there is one (much faster), else the Debug build; builds Release if neither exists.
setlocal
set "ROOT=%~dp0"

set "EXE=%ROOT%build-Release\bin\MagnumEditor.exe"
if not exist "%EXE%" set "EXE=%ROOT%build\bin\MagnumEditor.exe"
if not exist "%EXE%" (
    echo The editor is not built yet. Building the Release version, this takes a few minutes...
    call "%ROOT%build.bat" Release || (
        echo Build failed.
        pause
        exit /b 1
    )
    set "EXE=%ROOT%build-Release\bin\MagnumEditor.exe"
)

rem "start" detaches the editor, so this window closes right away.
start "" /d "%ROOT%" "%EXE%" %*
exit /b 0
