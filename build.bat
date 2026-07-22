@echo off
REM Compile the Elysium editor target (ElysiumUEEditor, Win64 Development) via UnrealBuildTool.
REM   build.bat            -> incremental build (default; only recompiles what changed)
REM   build.bat rebuild    -> full clean + build (forces every module to recompile)
REM   build.bat clean      -> delete build products only (no compile)
REM   build.bat <args...>  -> pass extra flags straight through to UnrealBuildTool
REM After it succeeds, launch with editor.bat / play.bat, or use Live Coding (Ctrl+Alt+F11)
REM inside the editor for the fastest inner loop.

setlocal
set "UE_ROOT=D:\Epic\UE_5.8"
set "BUILD=%UE_ROOT%\Engine\Build\BatchFiles\Build.bat"
set "REBUILD=%UE_ROOT%\Engine\Build\BatchFiles\Rebuild.bat"
set "CLEAN=%UE_ROOT%\Engine\Build\BatchFiles\Clean.bat"
set "PROJECT=%~dp0ElysiumUE.uproject"

set "TARGET=ElysiumUEEditor"
set "PLATFORM=Win64"
set "CONFIG=Development"

if not exist "%BUILD%" (
    echo [build] UnrealBuildTool not found at "%BUILD%".
    echo [build] Edit build.bat and set UE_ROOT to your engine install folder.
    pause
    exit /b 1
)

if /I "%~1"=="rebuild" (
    "%REBUILD%" %TARGET% %PLATFORM% %CONFIG% -Project="%PROJECT%" -WaitMutex
) else if /I "%~1"=="clean" (
    "%CLEAN%" %TARGET% %PLATFORM% %CONFIG% -Project="%PROJECT%" -WaitMutex
) else (
    "%BUILD%" %TARGET% %PLATFORM% %CONFIG% -Project="%PROJECT%" -WaitMutex %*
)

endlocal
exit /b %ERRORLEVEL%
