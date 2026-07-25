@echo off
REM Launch Elysium (Unreal). Optional first arg = map name under this repo's tools\out.
REM   play.bat                 : New Game: seed a fresh story context and enter sp_tutorial_1
REM                               at its `tutorial` landmark (add -ElysiumNewGame=0 to boot it bare)
REM   play.bat sp_tutorial_1   : that map explicitly, unseeded (the dev path)
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
REM -log opens a live external log window; -LogCmds raises the entity-world category to
REM Verbose from boot so the brush-body touch routing (RouteBrushTouch) prints as you play.
REM Cog is compiled in (non-Shipping) but boots dormant — press F1 to open it.
if "%~1"=="" (
    "%UE%" "%PROJECT%" -game -dx12 -windowed -resx=1600 -resy=900 -log -LogCmds="LogElysiumWorld Verbose"
) else (
    "%UE%" "%PROJECT%" -game -dx12 -windowed -resx=1600 -resy=900 -log -LogCmds="LogElysiumWorld Verbose" -ElysiumMap=%1
)
endlocal
