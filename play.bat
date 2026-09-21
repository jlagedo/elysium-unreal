@echo off
REM Launch the standalone Unreal game with a live log console and no debugger or harness attached.
REM Optional first argument selects an exported VtMB map; with no argument, game flow starts New Game.

setlocal EnableExtensions DisableDelayedExpansion
set "REPO_ROOT=%~dp0"
set "ENV_FILE=%REPO_ROOT%.elysium.local.env"

if not exist "%ENV_FILE%" (
    echo [play] Missing "%ENV_FILE%".
    echo [play] Copy dev\paths.example.env to .elysium.local.env and configure it first.
    exit /b 1
)

for /F "usebackq tokens=1,* delims==" %%A in ("%ENV_FILE%") do set "%%A=%%B"

if not defined ELYSIUM_UE_ROOT (
    echo [play] ELYSIUM_UE_ROOT is not configured in .elysium.local.env.
    exit /b 1
)

REM 0018 story 21-6: the game reads nothing outside the project, so there is no external root to
REM pass. What it needs is the deployed corpus, and that lives in the checkout.
set "CORPUS=%REPO_ROOT%Content\ElysiumCorpus"

set "UE=%ELYSIUM_UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%REPO_ROOT%ElysiumUE.uproject"

if not exist "%UE%" (
    echo [play] UnrealEditor.exe not found at "%UE%".
    exit /b 1
)
if not exist "%CORPUS%" (
    echo [play] Deployed corpus not found at "%CORPUS%".
    echo [play] Run the `uv run elysium import` lanes before launching the game.
    exit /b 1
)

set "MAP_ARG="
if not "%~1"=="" set "MAP_ARG=-ElysiumMap=%~1"

"%UE%" "%PROJECT%" -game -dx12 -windowed -resx=1600 -resy=900 -log %MAP_ARG%
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
