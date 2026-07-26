@echo off
REM Headless light-attribution probe. For every map given (default: every exported map), load it,
REM trace a ray fan from each WORLDLIGHTS source against the real built scene, and write
REM tools\out\_lights\<map>.probe.json — what each light is nearest, whether that thing emits,
REM how boxed-in the light is, and how much of its own patch it actually lights.
REM
REM   probe.bat                        : every map under tools\out
REM   probe.bat sp_tutorial_1 sm_hub_1 : just those
REM   PROBE_RAYS=128 in the environment : denser fan (default 64)
REM
REM One process per map (the probe reads the built scene; a fresh process guarantees a clean one).

setlocal enabledelayedexpansion
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"
set "OUT=%~dp0tools\out"
if "%PROBE_RAYS%"=="" set "PROBE_RAYS=64"

if not exist "%UE%" (
    echo [probe] UnrealEditor.exe not found at "%UE%".
    exit /b 1
)

set "MAPS=%*"
if "%MAPS%"=="" (
    for /d %%D in ("%OUT%\*") do (
        if exist "%%D\%%~nxD.lights" set "MAPS=!MAPS! %%~nxD"
    )
)

if "%MAPS%"=="" (
    echo [probe] no exported maps with a .lights sidecar under "%OUT%".
    exit /b 1
)

REM One line per launch: a caret continuation inside a parenthesised for-block is parsed
REM before the block runs, which splits the command and loses every argument after the first.
echo [probe] rays=%PROBE_RAYS%  maps:%MAPS%
for %%M in (%MAPS%) do call :probe %%M
echo [probe] done. results in tools\out\_lights\*.probe.json
endlocal
exit /b 0

:probe
echo [probe] %~1 ...
"%UE%" "%PROJECT%" -game -dx12 -windowed -resx=640 -resy=360 -nosound -ElysiumMap=%~1 -ElysiumProbe -ProbeRays=%PROBE_RAYS% -unattended -nosplash -stdout -FullStdOutLogOutput -LogCmds="LogElysiumLightProbe Log, LogElysiumProbe Log" 2>&1 | findstr /C:"lightprobe:" /C:"probe found"
exit /b 0
endlocal
