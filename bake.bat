@echo off
rem Bake one exported map into real Unreal assets under the /ElysiumBaked mount, in one
rem headless editor session (tools/bake_map.py -- the .uasset-bake spike).
rem
rem   bake.bat [map] [stages]
rem
rem   map     map name under tools/out (default sp_tutorial_1)
rem   stages  comma-separated subset of textures,materials,world,sky,props,level
rem           (default: all of them)
rem
rem Output lands in Plugins/ElysiumBaked/Content/ -- game-derived, gitignored, regenerable.
setlocal
set UE_ROOT=D:\Epic\UE_5.8
set PROJECT=%~dp0ElysiumUE.uproject

set MAP=%1
if "%MAP%"=="" set MAP=sp_tutorial_1

rem cmd splits arguments on commas, so a stage list arrives as separate tokens. Rejoin
rem everything after the map name back into one comma-separated value.
shift
set STAGES=
:stageloop
if "%1"=="" goto stagesdone
if "%STAGES%"=="" (set STAGES=%1) else (set STAGES=%STAGES%,%1)
shift
goto stageloop
:stagesdone
if "%STAGES%"=="" set STAGES=textures,materials,world,sky,props,level

echo [bake.bat] map=%MAP% stages=%STAGES%
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" ^
  -run=pythonscript -script="%~dp0tools\bake_map.py" ^
  -BakeMap=%MAP% -BakeStages=%STAGES% ^
  -unattended -nosplash -nopause -stdout -FullStdOutLogOutput
endlocal
