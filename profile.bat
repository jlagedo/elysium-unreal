@echo off
REM Headless render profiling for roadmap 0.1/0.2. Launches Elysium standalone, loads a
REM map, and drives the -ElysiumProfile harness (Source/ElysiumUE/Private/ElysiumProfiler):
REM it pins the camera to each configured vantage near spawn, warms up, captures N frames
REM through the CSV profiler (per-pass GPU stats via -csvGpuStats), writes a summary, and
REM exits. No human interaction. Then it runs the parser to emit the roadmap baseline table.
REM
REM   profile.bat                     sp_tutorial_1, all its vantages
REM   profile.bat sm_hub_1            one map's vantages
REM   profile.bat sp_tutorial_1 t2   a single vantage (index or name)
REM
REM Fixed 2560x1440 so runs are comparable over time. A real GPU/RHI is required (SM6/DX12);
REM this is NOT -nullrhi headless — a window renders, it just needs nobody at the keyboard.

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"
set "MAP=%~1"
if "%MAP%"=="" set "MAP=sp_tutorial_1"
set "CAM=%~2"

if not exist "%UE%" (
    echo [profile] UnrealEditor.exe not found at "%UE%".
    echo [profile] Edit profile.bat and set UE to your engine's UnrealEditor.exe path.
    exit /b 1
)

set "CAMARG="
if not "%CAM%"=="" set "CAMARG=-ProfileCam=%CAM%"

echo [profile] map=%MAP% cam=%CAM% (2560x1440, SM6/DX12)
"%UE%" "%PROJECT%" -game -dx12 -windowed -resx=2560 -resy=1440 ^
    -ElysiumMap=%MAP% -ElysiumProfile %CAMARG% ^
    -csvGpuStats -unattended -nosplash -nopause -stdout -FullStdOutLogOutput

echo [profile] engine exited; building report...
where python >nul 2>nul && python "%~dp0tools\profile_report.py" --map %MAP%

endlocal
