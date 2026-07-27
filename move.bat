@echo off
REM Headless movement regression (roadmap 4.7). Loads a map, replays a fixed command stream per
REM course through the real input router, samples the body every frame against real geometry, and
REM writes tools\out\_move\<map>.<course>.<hz>.csv + .json.
REM
REM   move.bat                       : every course on sp_tutorial_1 at 60 Hz
REM   move.bat duck                  : one course
REM   move.bat 120                   : every course at 120 Hz
REM   move.bat strafe 240            : the air-strafe course at 240 Hz
REM
REM The frame rate is FORCED (-UseFixedTimeStep -FPS), so a course is reproducible and the
REM 60/120/240 comparison means something: under the faithful variable step (elysium.move.FixedStep
REM 0) the three runs diverge -- that IS retail's frame-rate dependence -- and under a non-zero
REM FixedStep they agree. tools\move_diff.py is the comparator.
REM
REM Runs with -nullrhi: movement needs collision, not rendering, which is what keeps this cheap
REM enough to be a per-change check rather than a live session.

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"

set "COURSE=%~1"
set "HZ=%~2"

REM A bare number as the first argument is a frame rate, not a course name -- so `move.bat 120`
REM does the obvious thing and an empty quoted placeholder is never needed.
echo %COURSE%| findstr /r "^[0-9][0-9]*$" >nul 2>&1
if not errorlevel 1 (
    set "HZ=%COURSE%"
    set "COURSE="
)

if "%HZ%"=="" set "HZ=60"

set "MAP=sp_tutorial_1"

if not exist "%UE%" (
    echo [move] UnrealEditor.exe not found at "%UE%".
    exit /b 1
)

set "COURSEARG="
if not "%COURSE%"=="" set "COURSEARG=-MoveCourse=%COURSE%"

echo [move] %MAP% at %HZ% Hz %COURSEARG%
"%UE%" "%PROJECT%" -game -ElysiumMove -ElysiumMap=%MAP% -MoveHz=%HZ% %COURSEARG% ^
    -UseFixedTimeStep -FPS=%HZ% -nullrhi -unattended -nosplash -nosound ^
    -stdout -FullStdOutLogOutput

set "RC=%ERRORLEVEL%"
echo [move] done (exit %RC%); output under tools\out\_move
exit /b %RC%
