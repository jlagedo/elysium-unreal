[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Command = "doctor",
    [string]$UeRoot,
    [string]$VtmbRoot,
    [string]$WorkRoot,
    [string]$ExportRoot,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CommandArgs
)

$ErrorActionPreference = "Stop"
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$Project = Join-Path $RepoRoot "ElysiumUE.uproject"

function Import-LocalEnvironment {
    $path = Join-Path $RepoRoot ".elysium.local.env"
    if (!(Test-Path -LiteralPath $path)) {
        return
    }
    foreach ($line in Get-Content -LiteralPath $path) {
        $trimmed = $line.Trim()
        if (!$trimmed -or $trimmed.StartsWith("#")) {
            continue
        }
        $parts = $trimmed.Split("=", 2)
        if ($parts.Count -ne 2) {
            throw "invalid local environment line: $line"
        }
        if (![Environment]::GetEnvironmentVariable($parts[0], "Process")) {
            [Environment]::SetEnvironmentVariable($parts[0], $parts[1], "Process")
        }
    }
}

function Require-Directory([string]$Name) {
    $value = [Environment]::GetEnvironmentVariable($Name, "Process")
    if (!$value) {
        throw "$Name is not configured; copy dev/paths.example.env to .elysium.local.env"
    }
    $resolved = [IO.Path]::GetFullPath($value)
    if (!(Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "$Name does not exist: $resolved"
    }
    return $resolved
}

function Resolve-UE {
    $configured = [Environment]::GetEnvironmentVariable("ELYSIUM_UE_ROOT", "Process")
    if ($configured -and (Test-Path -LiteralPath $configured -PathType Container)) {
        return [IO.Path]::GetFullPath($configured)
    }
    $launcher = Join-Path $env:ProgramData "Epic/UnrealEngineLauncher/LauncherInstalled.dat"
    if (Test-Path -LiteralPath $launcher) {
        $data = Get-Content -Raw -LiteralPath $launcher | ConvertFrom-Json
        $entry = $data.InstallationList | Where-Object {
            $_.AppName -eq "UE_5.8" -or $_.ArtifactId -eq "UE_5.8"
        } | Select-Object -First 1
        if ($entry -and (Test-Path -LiteralPath $entry.InstallLocation)) {
            return [IO.Path]::GetFullPath($entry.InstallLocation)
        }
    }
    throw "ELYSIUM_UE_ROOT is not configured and UE 5.8 was not auto-detected"
}

function ConvertTo-ProcessArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) {
            ++$backslashes
            continue
        }
        if ($character -eq [char]34) {
            [void]$builder.Append('\' * ($backslashes * 2 + 1))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }
        if ($backslashes) {
            [void]$builder.Append('\' * $backslashes)
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashes) {
        [void]$builder.Append('\' * ($backslashes * 2))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    $argumentLine = (@($Arguments) | ForEach-Object {
        ConvertTo-ProcessArgument ([string]$_)
    }) -join " "
    $process = Start-Process -FilePath $Executable -ArgumentList $argumentLine `
        -NoNewWindow -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "$Executable exited with $($process.ExitCode)"
    }
}

function Unreal-Editor([switch]$Commandlet) {
    $ue = Resolve-UE
    $leaf = if ($Commandlet) { "UnrealEditor-Cmd.exe" } else { "UnrealEditor.exe" }
    $path = Join-Path $ue "Engine/Binaries/Win64/$leaf"
    if (!(Test-Path -LiteralPath $path)) {
        throw "Unreal editor executable not found: $path"
    }
    return $path
}

function Export-Root {
    $override = [Environment]::GetEnvironmentVariable("ELYSIUM_EXPORT_ROOT", "Process")
    if ($override) {
        return [IO.Path]::GetFullPath($override)
    }
    $work = Require-Directory "ELYSIUM_WORK_ROOT"
    return Join-Path $work "exports"
}

function Common-GameArgs {
    return @(
        $Project,
        "-game",
        "-ElysiumContentRoot=$(Export-Root)"
    )
}

if ($UeRoot) { $env:ELYSIUM_UE_ROOT = $UeRoot }
if ($VtmbRoot) { $env:ELYSIUM_VTMB_ROOT = $VtmbRoot }
if ($WorkRoot) { $env:ELYSIUM_WORK_ROOT = $WorkRoot }
if ($ExportRoot) { $env:ELYSIUM_EXPORT_ROOT = $ExportRoot }
Import-LocalEnvironment
$pipelineSrc = Join-Path $RepoRoot "pipeline/src"
$venvPython = Join-Path $RepoRoot "pipeline/.venv/Scripts/python.exe"
$oldPythonPath = [Environment]::GetEnvironmentVariable("PYTHONPATH", "Process")
$oldUnrealPythonPath = [Environment]::GetEnvironmentVariable("UE_PYTHONPATH", "Process")
$pathPrefix = "$RepoRoot;$pipelineSrc"
$env:PYTHONPATH = if ($oldPythonPath) { "$pathPrefix;$oldPythonPath" } else { $pathPrefix }
$env:UE_PYTHONPATH = if ($oldUnrealPythonPath) {
    "$pathPrefix;$oldUnrealPythonPath"
} else {
    $pathPrefix
}
if (!$env:ELYSIUM_EXPORT_ROOT -and $env:ELYSIUM_WORK_ROOT) {
    $env:ELYSIUM_EXPORT_ROOT = Join-Path $env:ELYSIUM_WORK_ROOT "exports"
}
Set-Location $RepoRoot

function Python-Executable {
    return $(if (Test-Path -LiteralPath $venvPython -PathType Leaf) {
        $venvPython
    } else {
        "python"
    })
}

switch ($Command.ToLowerInvariant()) {
    "bootstrap" {
        & (Join-Path $PSScriptRoot "bootstrap.ps1")
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        if (!(Test-Path -LiteralPath $venvPython -PathType Leaf)) {
            Invoke-Checked "python" @("-m", "venv", (Join-Path $RepoRoot "pipeline/.venv"))
        }
        Invoke-Checked $venvPython @("-m", "pip", "install", "--disable-pip-version-check",
            "-e", (Join-Path $RepoRoot "pipeline"))
        Invoke-Checked $venvPython @("-m", "elysium_pipeline.devtools.fetch_cpython27")
    }
    "doctor" {
        Invoke-Checked (Python-Executable) @("dev/check_repo_policy.py")
    }
    "build" {
        $mode = if ($CommandArgs.Count) { $CommandArgs[0].ToLowerInvariant() } else { "" }
        [string[]]$extra = if ($mode -in @("rebuild", "clean", "analyze")) {
            @($CommandArgs | Select-Object -Skip 1)
        } else {
            @($CommandArgs)
        }
        $batch = switch ($mode) {
            "rebuild" { "Rebuild.bat" }
            "clean" { "Clean.bat" }
            default { "Build.bat" }
        }
        if ($mode -eq "analyze") { $extra = @("-StaticAnalyzer=Default") + $extra }
        & (Join-Path (Resolve-UE) "Engine/Build/BatchFiles/$batch") `
            "ElysiumUEEditor" "Win64" "Development" "-Project=$Project" "-WaitMutex" @extra
        exit $LASTEXITCODE
    }
    "content" {
        $cmd = Unreal-Editor -Commandlet
        Invoke-Checked $cmd @(
            $Project,
            "-run=pythonscript",
            "-script=$(Join-Path $RepoRoot 'pipeline/unreal/build_content.py')",
            "-unattended", "-nosplash", "-nopause", "-stdout"
        )
        $editor = Unreal-Editor
        Invoke-Checked $editor @(
            $Project,
            "-ExecutePythonScript=$(Join-Path $RepoRoot 'pipeline/unreal/make_ui_fonts.py')",
            "-unattended", "-nosplash", "-nopause", "-stdout"
        )
        $fontRoot = Join-Path $RepoRoot "Content/VtMB/UI/Fonts"
        $fontAssets = @(
            "FF_SpectralSC_Regular.uasset",
            "FF_SpectralSC_SemiBold.uasset",
            "FF_Spectral_Regular.uasset",
            "FF_Spectral_Italic.uasset",
            "FF_Spectral_SemiBold.uasset",
            "FF_Inter_Regular.uasset",
            "FF_Inter_SemiBold.uasset"
        )
        $missingFonts = @($fontAssets | Where-Object {
            !(Test-Path -LiteralPath (Join-Path $fontRoot $_) -PathType Leaf)
        })
        if ($missingFonts.Count) {
            throw "font generation did not produce: $($missingFonts -join ', ')"
        }
    }
    "export" {
        Require-Directory "ELYSIUM_VTMB_ROOT" | Out-Null
        New-Item -ItemType Directory -Force -Path (Export-Root) | Out-Null
        Invoke-Checked (Python-Executable) (@("-m", "elysium_pipeline.exporters.export_all") + $CommandArgs)
    }
    "bake" {
        $map = if ($CommandArgs.Count -gt 0) { $CommandArgs[0] } else { "sp_tutorial_1" }
        $stages = if ($CommandArgs.Count -gt 1) {
            ($CommandArgs[1..($CommandArgs.Count - 1)] -join ",")
        } else {
            "textures,materials,world,sky,props,level"
        }
        Invoke-Checked (Unreal-Editor -Commandlet) @(
            $Project,
            "-run=pythonscript",
            "-script=$(Join-Path $RepoRoot 'pipeline/unreal/bake_map.py')",
            "-BakeMap=$map", "-BakeStages=$stages",
            "-unattended", "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput"
        )
    }
    "test" {
        $filter = if ($CommandArgs.Count) { $CommandArgs[0] } else { "Elysium." }
        if ($filter -ieq "Substrate") { $filter = "Elysium.Substrate." }
        if ($filter -ieq "Content") { $filter = "Elysium.Content." }
        $report = Join-Path (Export-Root) "_tests"
        Invoke-Checked (Unreal-Editor -Commandlet) @(
            $Project,
            "-ElysiumContentRoot=$(Export-Root)",
            "-ExecCmds=Automation RunTest $filter;Quit",
            "-ReportExportPath=$report",
            "-unattended", "-nopause", "-nosplash", "-nullrhi",
            "-stdout", "-FullStdOutLogOutput"
        )
    }
    "editor" {
        Invoke-Checked (Unreal-Editor) (@($Project, "-ElysiumContentRoot=$(Export-Root)") + $CommandArgs)
    }
    "play" {
        $args = Common-GameArgs
        $args += @("-dx12", "-windowed", "-resx=1600", "-resy=900", "-log",
            "-LogCmds=LogElysiumWorld Verbose, LogElysiumIO Verbose")
        if ($CommandArgs.Count) { $args += "-ElysiumMap=$($CommandArgs[0])" }
        Invoke-Checked (Unreal-Editor) $args
    }
    "profile" {
        $map = if ($CommandArgs.Count) { $CommandArgs[0] } else { "sp_tutorial_1" }
        $args = Common-GameArgs
        $args += @("-dx12", "-windowed", "-resx=2560", "-resy=1440",
            "-ElysiumMap=$map", "-ElysiumProfile", "-csvGpuStats",
            "-unattended", "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput")
        if ($CommandArgs.Count -gt 1) { $args += "-ProfileCam=$($CommandArgs[1])" }
        Invoke-Checked (Unreal-Editor) $args
        Invoke-Checked (Python-Executable) @("-m", "elysium_pipeline.validation.profile_report", "--map", $map)
    }
    "probe" {
        $maps = @($CommandArgs)
        if (!$maps.Count) {
            $maps = Get-ChildItem -LiteralPath (Export-Root) -Directory | Where-Object {
                Test-Path -LiteralPath (Join-Path $_.FullName "$($_.Name).lights")
            } | ForEach-Object Name
        }
        if (!$maps.Count) { throw "no exported maps with a .lights sidecar" }
        $rays = if ($env:PROBE_RAYS) { $env:PROBE_RAYS } else { "64" }
        foreach ($map in $maps) {
            $args = Common-GameArgs
            $args += @("-dx12", "-windowed", "-resx=640", "-resy=360", "-nosound",
                "-ElysiumMap=$map", "-ElysiumProbe", "-ProbeRays=$rays",
                "-unattended", "-nosplash", "-stdout", "-FullStdOutLogOutput")
            Invoke-Checked (Unreal-Editor) $args
        }
    }
    "shots" {
        $map = if ($CommandArgs.Count) { $CommandArgs[0] } else { "sp_tutorial_1" }
        $args = Common-GameArgs
        $args += @("-dx12", "-RenderOffScreen", "-ForceRes", "-windowed",
            "-ResX=2560", "-ResY=1440", "-ElysiumMap=$map", "-ElysiumShots",
            "-unattended", "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput")
        if ($CommandArgs.Count -gt 1) { $args += "-ShotCam=$($CommandArgs[1])" }
        Invoke-Checked (Unreal-Editor) $args
    }
    "move" {
        $course = if ($CommandArgs.Count) { $CommandArgs[0] } else { "" }
        $hz = if ($CommandArgs.Count -gt 1) { $CommandArgs[1] } else { "60" }
        if ($course -match "^\d+$") { $hz = $course; $course = "" }
        $args = Common-GameArgs
        $args += @("-ElysiumMove", "-ElysiumMap=sp_tutorial_1", "-MoveHz=$hz",
            "-UseFixedTimeStep", "-FPS=$hz", "-nullrhi", "-unattended",
            "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput")
        if ($course) { $args += "-MoveCourse=$course" }
        Invoke-Checked (Unreal-Editor) $args
    }
    "greenroom" {
        $case = if ($CommandArgs.Count) { $CommandArgs[0] } else { "player" }
        $map = if ($CommandArgs.Count -gt 1) { $CommandArgs[1] } elseif ($case -in @("embrace", "props")) { "sp_theatre" } else { "sp_tutorial_1" }
        $args = Common-GameArgs
        $args += @("-dx12", "-RenderOffScreen", "-ForceRes", "-windowed",
            "-ResX=1920", "-ResY=1080", "-ElysiumMap=$map", "-ElysiumGreenRoom",
            "-GreenRoomCase=$case", "-GreenRoomSettle=15",
            "-unattended", "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput")
        Invoke-Checked (Unreal-Editor) $args
    }
    "modelroom" {
        if ($CommandArgs.Count -lt 2) {
            throw "modelroom requires <mesh-stem> <clip> [anim-set bone-root] [map]"
        }
        $stem, $clip = $CommandArgs[0], $CommandArgs[1]
        $animSet = ""; $boneRoot = ""; $map = "sp_tutorial_1"
        if ($CommandArgs.Count -gt 2 -and $CommandArgs[2].StartsWith("models/")) {
            $animSet = $CommandArgs[2]
            if ($CommandArgs.Count -lt 4) { throw "a cinematic anim-set requires a bone root" }
            $boneRoot = $CommandArgs[3]
            if ($CommandArgs.Count -gt 4) { $map = $CommandArgs[4] }
        } elseif ($CommandArgs.Count -gt 2) {
            $map = $CommandArgs[2]
        }
        $args = Common-GameArgs
        $args += @("-dx12", "-RenderOffScreen", "-ForceRes", "-windowed",
            "-ResX=1920", "-ResY=1080", "-ElysiumMap=$map", "-ElysiumGreenRoom",
            "-GreenRoomCase=review", "-GreenRoomStem=$stem", "-GreenRoomClip=$clip",
            "-GreenRoomAnimSet=$animSet", "-GreenRoomBoneRoot=$boneRoot", "-GreenRoomSettle=8",
            "-unattended", "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput")
        Invoke-Checked (Unreal-Editor) $args
        $review = Join-Path (Export-Root) "_greenroom/review"
        Invoke-Checked (Python-Executable) @("-m", "elysium_pipeline.validation.greenroom_contact_sheet", $review)
        Invoke-Item (Join-Path $review "review_sheet.png")
    }
    "research" {
        if (!$CommandArgs.Count) { throw "research requires a case name" }
        $case = $CommandArgs[0]
        $caseRoot = Join-Path $RepoRoot "research/cases/$case"
        if (!(Test-Path -LiteralPath $caseRoot)) { throw "unknown research case: $case" }
        $spec = if ($CommandArgs.Count -gt 1 -and $CommandArgs[1].EndsWith(".json")) {
            $CommandArgs[1]
        } else {
            (Get-ChildItem -LiteralPath (Join-Path $caseRoot "specs") -Filter "*.json" | Select-Object -First 1).FullName
        }
        $extra = if ($CommandArgs.Count -gt 1 -and !$CommandArgs[1].EndsWith(".json")) {
            $CommandArgs[1..($CommandArgs.Count - 1)]
        } elseif ($CommandArgs.Count -gt 2) {
            $CommandArgs[2..($CommandArgs.Count - 1)]
        } else { @() }
        Invoke-Checked (Python-Executable) (@("research/tooling/ghidra/driver/ghidra_context.py", $spec) + $extra)
    }
    "ide" {
        if (!$CommandArgs.Count -or $CommandArgs[0] -ne "vscode") {
            throw "ide currently supports only 'vscode'"
        }
        $ideArgs = if ($CommandArgs.Count -gt 1) { @($CommandArgs[1..($CommandArgs.Count - 1)]) } else { @() }
        Invoke-Checked (Python-Executable) (@("-m", "elysium_pipeline.devtools.setup_vscode") + $ideArgs)
    }
    "mcp" {
        Invoke-Checked (Python-Executable) (@("-m", "elysium_pipeline.devtools.mcp_proxy") + $CommandArgs)
    }
    default {
        throw "unknown command '$Command'"
    }
}
