@echo off
REM Headless screenshot capture for the regression harness (roadmap P2.9). Launches Elysium
REM standalone, loads a map, and drives the -ElysiumShots harness (Source\ElysiumUE\Private\
REM ElysiumShotRun): it pins the camera to each configured vantage (the SAME vantages profile.bat
REM uses), lets the frame settle (Lumen accumulation + shader compile), captures the viewport to a
REM PNG, writes a manifest, and exits. No human interaction.
REM
REM   shots.bat                     sp_tutorial_1, all its vantages
REM   shots.bat sm_pawnshop_1       one map's vantages
REM   shots.bat sp_tutorial_1 t2    a single vantage (index or name)
REM
REM Shots land under tools\out\_shots\<map>\ (gitignored — derived from the user's own VtMB
REM install, never committed). A real GPU/RHI is required (SM6/DX12); this is NOT -nullrhi, a
REM window renders, it just needs nobody at the keyboard. Fixed 2560x1440 so shots are comparable
REM over time. Diff a run against a kept baseline to catch a look regression.

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"
set "MAP=%~1"
if "%MAP%"=="" set "MAP=sp_tutorial_1"
set "CAM=%~2"

if not exist "%UE%" (
    echo [shots] UnrealEditor.exe not found at "%UE%".
    echo [shots] Edit shots.bat and set UE to your engine's UnrealEditor.exe path.
    exit /b 1
)

set "CAMARG="
if not "%CAM%"=="" set "CAMARG=-ShotCam=%CAM%"

echo [shots] map=%MAP% cam=%CAM% (2560x1440, SM6/DX12)
"%UE%" "%PROJECT%" -game -dx12 -windowed -resx=2560 -resy=1440 ^
    -ElysiumMap=%MAP% -ElysiumShots %CAMARG% ^
    -unattended -nosplash -nopause -stdout -FullStdOutLogOutput

echo [shots] engine exited; shots under tools\out\_shots\%MAP%\
endlocal
