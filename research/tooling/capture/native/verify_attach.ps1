param(
    [Parameter(Mandatory = $true)]
    [string] $Launcher,
    [Parameter(Mandatory = $true)]
    [string] $Target,
    [Parameter(Mandatory = $true)]
    [string] $BinDir,
    [Parameter(Mandatory = $true)]
    [string] $Probe
)

$ErrorActionPreference = "Stop"
$targetProcess = $null
try {
    $targetProcess = Start-Process `
        -FilePath $Target `
        -ArgumentList @(
            "--initial-delay-ms", "1000",
            "--module-delay-ms", "10",
            "--lifetime-ms", "10000"
        ) `
        -WorkingDirectory $BinDir `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Milliseconds 200

    $output = & $Launcher `
        --attach-pid $targetProcess.Id `
        --distribution synthetic-test `
        --probe-host $Probe 2>&1
    $launcherExit = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($launcherExit -ne 0) {
        throw "attach launcher exited with $launcherExit"
    }
    $joined = $output -join "`n"
    if ($joined -notmatch "event=bootstrap_ready mode=attached") {
        throw "attach did not report the versioned ready handshake"
    }
    if ($joined -notmatch "event=attach_complete mode=attached") {
        throw "attach did not report attached session metadata"
    }
    $targetProcess.Refresh()
    if ($targetProcess.HasExited) {
        throw "non-owning attach terminated the target"
    }
} finally {
    if ($null -ne $targetProcess) {
        $targetProcess.Refresh()
        if (-not $targetProcess.HasExited) {
            Stop-Process -Id $targetProcess.Id
            $targetProcess.WaitForExit()
        }
    }
}
