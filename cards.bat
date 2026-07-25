@echo off
REM Headless Lumen card bake (docs\lumen-coverage-spike.md). Launches Elysium standalone and drives
REM the -ElysiumCards harness (Source\ElysiumUE\Private\ElysiumCardRun): it loads each map exactly
REM as the game does, runs the editor surfel card builder over every world chunk and prop model the
REM load produced, writes tools\out\<map>\<map>.cards, and travels to the next map. No interaction.
REM
REM   cards.bat                     every exported map under tools\out
REM   cards.bat sp_tutorial_1       one map
REM
REM Why an engine run and not a tools\ Python stage: the card builder is
REM IMeshUtilities::GenerateCardRepresentationData, which ray-traces the mesh through Embree and so
REM exists only in the editor. Baking here and shipping a sidecar is what keeps Embree out of the
REM shipping build. It is still an offline step -- the runtime only ever reads the file.
REM
REM The bake fits cards to the meshes the GAME builds, by doing a real map load, so there is no
REM second chunker to keep in step with BuildWorldChunks. Change elysium.LumenCardCellCm and re-run;
REM until you do, the old sidecar's keys miss and the runtime falls back to bounds cards.
REM
REM Requires an EDITOR-target build (build.bat) -- the builder is not in a shipping binary. Sidecars
REM land beside the export in tools\out (gitignored -- derived from the user's own VtMB install).
REM Roughly 35 s per map on sp_tutorial_1-sized geometry.

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"
set "MAP=%~1"

if not exist "%UE%" (
    echo [cards] UnrealEditor.exe not found at "%UE%".
    echo [cards] Edit cards.bat and set UE to your engine's UnrealEditor.exe path.
    exit /b 1
)

set "MAPARG="
if not "%MAP%"=="" set "MAPARG=-ElysiumMap=%MAP%"

if "%MAP%"=="" (echo [cards] baking every exported map) else (echo [cards] baking %MAP%)

REM -RenderOffScreen so no window steals focus; a real RHI is still needed because the bake reads
REM the LODs the normal mesh build produces. -ElysiumNewGame=0 boots the map bare (no story seed).
"%UE%" "%PROJECT%" -game -dx12 -RenderOffScreen -windowed -ResX=1280 -ResY=720 ^
    %MAPARG% -ElysiumNewGame=0 -ElysiumCards -NoElysiumMcp ^
    -unattended -nosplash -nopause -stdout -FullStdOutLogOutput

echo [cards] engine exited; sidecars under tools\out\^<map^>\^<map^>.cards
endlocal
exit /b %ERRORLEVEL%
