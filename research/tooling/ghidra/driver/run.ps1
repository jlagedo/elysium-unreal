<#
  Ghidra headless runner for the VtMB binaries (menu / GameUI RE).

  Usage (from repo root):
    uv run elysium research <case>

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
  [string]$ProjName = "vtmb",
  [string]$FidDb,                        # FID database attached before analysis (default: the CRT db)
  [switch]$NoFid,                        # do not attach any FID database
  [switch]$AnalyzeAll,                   # analyze every program already in the project
  [switch]$Recursive,                    # descend into the import path's folders and container files
  [string]$Processor,                    # force a LanguageID on import (e.g. x86:LE:32:default)
  [string]$Cspec                         # force a compiler spec on import (e.g. windows)
)

$workRoot = [Environment]::GetEnvironmentVariable("ELYSIUM_WORK_ROOT", "Process")
if (!$workRoot) { throw "ELYSIUM_WORK_ROOT is not configured" }
if (!$GhidraDir) { $GhidraDir = Join-Path $workRoot "cache/ghidra_12.1.2_PUBLIC" }
if (!$ProjDir) { $ProjDir = Join-Path $workRoot "research/ghidra/project" }
if (!$FidDb) { $FidDb = Join-Path $workRoot "research/ghidra/fid/vc6sp5.fidb" }
$scriptDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../scripts"))
$headless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
New-Item -ItemType Directory -Force -Path $ProjDir | Out-Null

# Pre-scripts run before auto-analysis — the only place analysis options can still be
# changed (EnableAIF), and the only place a FID database can be attached in time for the
# Function ID analyzer to query it. AttachFid goes last because analyzeHeadless reads a
# -preScript's arguments as every following token up to the next option.
$pre = @()
if ($PreScript) { $pre += @("-preScript", "$PreScript.java") }
$fidMissing = $false
if (!$NoFid) {
  if (Test-Path $FidDb) {
    $pre += @("-preScript", "AttachFid.java", "fidb=$($FidDb.Replace('\', '/'))")
  }
  else {
    # Absent is not the same as opted out. Without the database the Function ID analyzer
    # falls back to the ones Ghidra ships, none of which covers a VC6 service pack, and the
    # program comes out with a fraction of its C runtime named and no indication why.
    $fidMissing = $true
  }
}
function Show-MissingFidWarning {
  if (!$fidMissing) { return }
  Write-Warning "No FID database at $FidDb - this program's C runtime will be only partly named."
  Write-Warning "Build it with: uv run elysium research crt_fid stage; uv run elysium research crt_fid build"
  Write-Warning "Pass -NoFid to run without it deliberately."
}

if ($Import) {
  Show-MissingFidWarning
  # One argument vector, built up front: splatting a possibly-empty array inline makes
  # PowerShell hand analyzeHeadless a bare "-" and the parse dies before Ghidra starts.
  $argv = @($ProjDir, $ProjName, "-import", $Import, "-overwrite")
  if ($Recursive) { $argv += "-recursive" }
  # A COFF object carries no compiler identity, so Ghidra's opinion service picks gcc and
  # the whole import is analyzed with the wrong calling conventions. FID refuses to build a
  # library whose members disagree on the compiler spec, which is how this surfaces.
  if ($Processor) { $argv += @("-processor", $Processor) }
  if ($Cspec) { $argv += @("-cspec", $Cspec) }
  $argv += @("-scriptPath", $scriptDir) + $pre
  & $headless @argv
}
elseif ($AnalyzeAll) {
  Show-MissingFidWarning
  $argv = @($ProjDir, $ProjName, "-process", "-recursive", "-scriptPath", $scriptDir) + $pre
  & $headless @argv
}
elseif ($Script) {
  # -process matches only the project root unless -recursive is given, so a program
  # imported from a container file (which nests under the container's own folders) is
  # reported as "not found" without it.
  $sa = if ($ScriptArgs) { $ScriptArgs.Split(" ", [StringSplitOptions]::RemoveEmptyEntries) } else { @() }
  $argv = @($ProjDir, $ProjName, "-process", $Program, "-noanalysis")
  if ($Recursive) { $argv += "-recursive" }
  $argv += @("-scriptPath", $scriptDir, "-postScript", "$Script.java") + $sa
  & $headless @argv
}
else {
  Write-Host "Nothing to do. Pass -Import <dll>, -AnalyzeAll or -Script <name>."
}
