@echo off
REM Rebuild every committed Content/ asset (M_World_*, M_Additive, M_Sky, Elysium.umap) by running
REM tools/build_content.py in one headless editor session -- the umbrella so no offline
REM asset generator is forgotten. Run it standalone, or let export_all.py invoke it (default).
REM   content.bat          : rebuild all committed assets

setlocal
set "UE_ROOT=D:\Epic\UE_5.8"
set "UECMD=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "PROJECT=%~dp0ElysiumUE.uproject"
set "SCRIPT=%~dp0tools\build_content.py"

if not exist "%UECMD%" (
    echo [content] UnrealEditor-Cmd not found at "%UECMD%".
    echo [content] Edit content.bat and set UE_ROOT to your engine install folder.
    exit /b 1
)

"%UECMD%" "%PROJECT%" -run=pythonscript -script="%SCRIPT%" -unattended -nosplash -nopause -stdout

endlocal
exit /b %ERRORLEVEL%
