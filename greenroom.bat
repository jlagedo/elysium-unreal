@echo off
REM Deterministic body/clip validation. Runs the real skeletal loader, cinematic-bank binding,
REM absolute seek and scripted-camera/player-material path. Individual cases use an isolated lit
REM stage; embrace uses real sp_theatre placement and camera tracks. Captures PNGs plus a manifest.
REM
REM   greenroom.bat player       one player body + Bip01 clip (default)
REM   greenroom.bat sire         final default Brujah-female sire + Bip02 clip
REM   greenroom.bat opening      all five bodies at authored animation-root placement
REM   greenroom.bat embrace      real theatre placement + paired camera tracks
REM   greenroom.bat courtroom    seated Vampire4 + LaCroix authored-space facing oracle
REM   greenroom.bat props        all opening prop clips, one model/animation at a time
REM   greenroom.bat all          each body individually
REM   greenroom.bat placeholder  diagnostic only: the temporary rainbow doppleganger

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"
set "CASE=%~1"
if "%CASE%"=="" set "CASE=player"
set "MAP=%~2"
if "%MAP%"=="" set "MAP=sp_tutorial_1"
if /I "%CASE%"=="embrace" if "%~2"=="" set "MAP=sp_theatre"
if /I "%CASE%"=="props" if "%~2"=="" set "MAP=sp_theatre"

if not exist "%UE%" (
    echo [greenroom] UnrealEditor.exe not found at "%UE%".
    exit /b 1
)

echo [greenroom] case=%CASE% map=%MAP% (1920x1080, SM6/DX12, off-screen)
"%UE%" "%PROJECT%" -game -dx12 -RenderOffScreen -ForceRes -windowed -ResX=1920 -ResY=1080 ^
    -ElysiumMap=%MAP% -ElysiumGreenRoom -GreenRoomCase=%CASE% -GreenRoomSettle=15 ^
    -unattended -nosplash -nopause -stdout -FullStdOutLogOutput

echo [greenroom] engine exited; captures under tools\out\_greenroom\%CASE%\
endlocal
