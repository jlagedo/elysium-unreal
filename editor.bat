@echo off
REM Open the Elysium project in the Unreal editor (for PIE, profiling, scene edits).
REM Press Play in the toolbar to run; the game mode loads sp_tutorial_1 at runtime.

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"

if not exist "%UE%" (
    echo [editor] UnrealEditor.exe not found at "%UE%".
    echo [editor] Edit editor.bat and set UE to your engine's UnrealEditor.exe path.
    pause
    exit /b 1
)

"%UE%" "%PROJECT%" %*
endlocal
