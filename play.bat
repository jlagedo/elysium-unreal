@echo off
REM Launch Elysium (Unreal). Optional first arg = map name under this repo's tools\out.
REM   play.bat                 -> sp_tutorial_1 (the exported vertical slice)
REM   play.bat sp_tutorial_1   -> that map explicitly
REM WASD + E/Q (or Space/Ctrl) to fly, mouse to look. Only sp_tutorial_1 is exported today.

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"

if not exist "%UE%" (
    echo [play] UnrealEditor.exe not found at "%UE%".
    echo [play] Edit play.bat and set UE to your engine's UnrealEditor.exe path.
    pause
    exit /b 1
)

REM -dx12 forces the DX12 RHI so the SM6 features (Lumen, MegaLights, VSM, ray tracing)
REM run; without it the renderer can fall back to DX11/SM5 and they are all off.
if "%~1"=="" (
    "%UE%" "%PROJECT%" -game -dx12 -windowed -resx=1600 -resy=900
) else (
    "%UE%" "%PROJECT%" -game -dx12 -windowed -resx=1600 -resy=900 -ElysiumMap=%1
)
endlocal
