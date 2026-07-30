@echo off
REM Human visual review for one skeletal model and one exact animation.
REM
REM Ordinary resolved clip:
REM   modelroom.bat <mesh-stem> <clip> [map]
REM
REM Cinematic bank clip:
REM   modelroom.bat <mesh-stem> <clip> <anim-set-model> <bone-root> [map]
REM
REM Examples:
REM   modelroom.bat brujah_male_armor_0 Stance_Neutral
REM   modelroom.bat ventrue_female_armor_1 entire_scene ^
REM     models/cinematic/santa_monica/courtroom/courtroom_bip5.mdl Bip01

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"
set "STEM=%~1"
set "CLIP=%~2"
set "ANIMSET=%~3"
set "BONEROOT=%~4"
set "MAP=%~5"

if "%STEM%"=="" goto :usage
if "%CLIP%"=="" goto :usage

if "%ANIMSET%"=="" (
    set "MAP=%~3"
    set "BONEROOT="
)
if not "%ANIMSET%"=="" if /I not "%ANIMSET:~0,7%"=="models/" (
    set "MAP=%ANIMSET%"
    set "ANIMSET="
    set "BONEROOT="
)
if "%MAP%"=="" set "MAP=sp_tutorial_1"

if not exist "%UE%" (
    echo [modelroom] UnrealEditor.exe not found at "%UE%".
    exit /b 1
)

echo [modelroom] model=%STEM% clip=%CLIP% map=%MAP%
if not "%ANIMSET%"=="" echo [modelroom] cinematic=%ANIMSET% root=%BONEROOT%

"%UE%" "%PROJECT%" -game -dx12 -RenderOffScreen -ForceRes -windowed -ResX=1920 -ResY=1080 ^
    -ElysiumMap=%MAP% -ElysiumGreenRoom -GreenRoomCase=review ^
    -GreenRoomStem="%STEM%" -GreenRoomClip="%CLIP%" ^
    -GreenRoomAnimSet="%ANIMSET%" -GreenRoomBoneRoot="%BONEROOT%" -GreenRoomSettle=8 ^
    -unattended -nosplash -nopause -stdout -FullStdOutLogOutput
set "RUN_RESULT=%ERRORLEVEL%"

if not "%RUN_RESULT%"=="0" (
    echo [modelroom] render failed with exit %RUN_RESULT%.
    exit /b %RUN_RESULT%
)

python "%~dp0tools\greenroom_contact_sheet.py" "%~dp0tools\out\_greenroom\review"
if errorlevel 1 exit /b 1

set "SHEET=%~dp0tools\out\_greenroom\review\review_sheet.png"
echo [modelroom] opening "%SHEET%"
start "" "%SHEET%"
exit /b 0

:usage
echo Usage:
echo   modelroom.bat ^<mesh-stem^> ^<clip^> [map]
echo   modelroom.bat ^<mesh-stem^> ^<clip^> ^<anim-set-model^> ^<bone-root^> [map]
exit /b 2
