<#
  Ghidra headless runner for the VtMB binaries (menu / GameUI RE).

  Usage (from repo root):
    dev/elysium.ps1 research <case>

  Everything derived (the project DB under project\, the $ELYSIUM_EXPORT_ROOT\ dumps) is gitignored;
  only the .java scripts + this runner + README are committed.
#>
param(
  [string]$Import,                       # path to a binary to import+analyze (optional)
  [string]$PreScript,                    # GhidraScript run BEFORE analysis (e.g. EnableAIF to set options)
  [string]$Script,                       # GhidraScript name (without .java) to post-run
  [string]$ScriptArgs = "",              # scriptArgs string passed through to the script ($Args is reserved)
  [string]$Program = "GameUI.dll",       # program name inside the project (for -process)
  [string]$GhidraDir,
  [string]$ProjDir,
  [string]$ProjName = "vtmb"
)

$workRoot = [Environment]::GetEnvironmentVariable("ELYSIUM_WORK_ROOT", "Process")
if (!$workRoot) { throw "ELYSIUM_WORK_ROOT is not configured" }
if (!$GhidraDir) { $GhidraDir = Join-Path $workRoot "cache/ghidra_12.1.2_PUBLIC" }
if (!$ProjDir) { $ProjDir = Join-Path $workRoot "research/ghidra/project" }
$scriptDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../scripts"))
$headless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
New-Item -ItemType Directory -Force -Path $ProjDir | Out-Null

if ($Import) {
  if ($PreScript) {
    # A pre-script runs before auto-analysis — the only place analysis options can
    # still be changed (e.g. EnableAIF, for binaries whose code is reached solely
    # through vtables and so is left undisassembled by the default analyzers).
    & $headless $ProjDir $ProjName -import $Import -overwrite `
        -scriptPath $scriptDir -preScript "$PreScript.java"
  }
  else {
    & $headless $ProjDir $ProjName -import $Import -overwrite
  }
}
elseif ($Script) {
  $sa = if ($ScriptArgs) { $ScriptArgs.Split(" ", [StringSplitOptions]::RemoveEmptyEntries) } else { @() }
  & $headless $ProjDir $ProjName -process $Program -noanalysis `
      -scriptPath $scriptDir -postScript "$Script.java" @sa
}
else {
  Write-Host "Nothing to do. Pass -Import <dll> or -Script <name>."
}
