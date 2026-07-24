@echo off
REM Run the Elysium automation tests headless (roadmap P2.8). Drives Unreal's own automation
REM runner over the ElysiumUE test suite and exports a JSON+HTML report.
REM
REM   test.bat                 -> the whole Elysium suite (Substrate + Content)
REM   test.bat Substrate       -> just the content-free tier (variant/expr/kv/queue/registry/IO)
REM   test.bat Content         -> just the content-gated tier (real .ents parse; self-skips if unexported)
REM   test.bat Elysium.Substrate.Expr   -> one test by its full dotted name
REM
REM The Substrate tier runs under -nullrhi (no GPU, no content). The Content tier reads the
REM exported maps under tools\out and self-skips any map the pipeline has not exported, so a
REM checkout with an empty tools\out still passes. Requires the editor target to be built first
REM (build.bat) — the tests compile into UnrealEditor-ElysiumUE with WITH_DEV_AUTOMATION_TESTS.

setlocal
set "UE=D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"

set "FILTER=%~1"
if "%FILTER%"=="" set "FILTER=Elysium."
REM Bare "Substrate"/"Content" are shorthands for the two suites.
if /I "%FILTER%"=="Substrate" set "FILTER=Elysium.Substrate."
if /I "%FILTER%"=="Content"   set "FILTER=Elysium.Content."

set "REPORT=%~dp0tools\out\_tests"

if not exist "%UE%" (
    echo [test] UnrealEditor-Cmd.exe not found at "%UE%".
    echo [test] Edit test.bat and set UE to your engine's UnrealEditor-Cmd.exe path.
    exit /b 1
)

echo [test] running automation tests matching "%FILTER%*"
"%UE%" "%PROJECT%" ^
    -ExecCmds="Automation RunTest %FILTER%;Quit" ^
    -ReportExportPath="%REPORT%" ^
    -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput

set "RESULT=%ERRORLEVEL%"
echo [test] done (exit %RESULT%); report under tools\out\_tests\index.html
endlocal & exit /b %RESULT%
