param(
    [Parameter(Mandatory = $true)]
    [string] $Launcher,
    [Parameter(Mandatory = $true)]
    [string] $Target,
    [Parameter(Mandatory = $true)]
    [string] $BinDir,
    [Parameter(Mandatory = $true)]
    [string] $Probe,
    [Parameter(Mandatory = $true)]
    [string] $Collector,
    [Parameter(Mandatory = $true)]
    [string] $OutputDir,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 1000)]
    [int] $Cycles
)

$ErrorActionPreference = "Stop"

function Get-SelfHandleCount {
    $process = [System.Diagnostics.Process]::GetCurrentProcess()
    try {
        return $process.HandleCount
    } finally {
        $process.Dispose()
    }
}

function Assert-ProcessExited {
    param(
        [Parameter(Mandatory = $true)]
        [int] $ProcessId,
        [Parameter(Mandatory = $true)]
        [string] $Role
    )
    try {
        $process = [System.Diagnostics.Process]::GetProcessById($ProcessId)
    } catch [System.ArgumentException] {
        return
    }
    try {
        if (-not $process.HasExited) {
            throw "$Role process $ProcessId remains alive"
        }
    } finally {
        $process.Dispose()
    }
}

function Read-Contract {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "missing contract artifact: $Path"
    }
    return ConvertFrom-StringData -StringData (
        Get-Content -LiteralPath $Path -Raw
    )
}

function Assert-OrderedEvents {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Output
    )
    $events = @(
        "event=process_suspended mode=launched",
        "event=bootstrap_ready mode=launched",
        "event=all_modules_ready",
        "event=capture_started",
        "event=capture_stop_requested",
        "event=capture_stop_received",
        "event=shutdown_complete modules=3",
        "event=trace_finalized",
        "event=supervision_finalized mode=launched"
    )
    $position = -1
    foreach ($event in $events) {
        $next = $Output.IndexOf(
            $event,
            $position + 1,
            [System.StringComparison]::Ordinal
        )
        if ($next -lt 0) {
            throw "lifecycle output lacks ordered event '$event':`n$Output"
        }
        $position = $next
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$firstCycleHandles = 0
$lastCycleHandles = 0
for ($cycle = 1; $cycle -le $Cycles; ++$cycle) {
    $label = "{0:D3}" -f $cycle
    $report = Join-Path $OutputDir "$label-finalization.txt"
    $trace = Join-Path $OutputDir "$label-trace.txt"
    foreach ($artifact in @($report, "$report.tmp", $trace, "$trace.tmp")) {
        Remove-Item -LiteralPath $artifact -Force -ErrorAction SilentlyContinue
    }

    $outputLines = & $Launcher `
        --executable $Target `
        --working-directory $BinDir `
        --distribution synthetic-soak `
        --startup-profile direct `
        --probe-host $Probe `
        --collector $Collector `
        --collector-argument --trace `
        --collector-argument $trace `
        --collector-argument --request-target-stop-ms `
        --collector-argument 1 `
        --finalization $report `
        --timeout-ms 5000 `
        --supervise `
        -- `
        --initial-delay-ms 0 `
        --module-delay-ms 0 `
        --lifetime-ms 0 `
        --capture-stop-timeout-ms 5000 2>&1
    $launcherExit = $LASTEXITCODE
    $output = $outputLines -join "`n"
    if ($launcherExit -ne 0) {
        throw "cycle $cycle launcher exited with ${launcherExit}:`n$output"
    }
    Assert-OrderedEvents -Output $output
    if ([regex]::Matches($output, "event=module_unloaded").Count -ne 3) {
        throw "cycle $cycle did not unload exactly three synthetic modules"
    }

    $finalization = Read-Contract -Path $report
    foreach ($expectation in @{
        contract = "elysium.retail-capture-finalization"
        version = "1"
        state = "complete"
        reason = "process-exit"
        partial = "0"
        process_resumed = "1"
        collector_started = "1"
        process_exit_code = "0"
        collector_exit_code = "0"
    }.GetEnumerator()) {
        if ($finalization[$expectation.Key] -ne $expectation.Value) {
            throw (
                "cycle $cycle finalization $($expectation.Key) is " +
                "'$($finalization[$expectation.Key])'"
            )
        }
    }
    if ([int]$finalization.module_notifications -lt 6) {
        throw "cycle $cycle observed fewer than six module transitions"
    }

    $capture = Read-Contract -Path $trace
    foreach ($expectation in @{
        contract = "elysium.synthetic-capture-trace"
        version = "1"
        state = "complete"
        target_pid = $finalization.process_id
        capture_started = "1"
        stop_requested = "1"
        target_exited = "1"
    }.GetEnumerator()) {
        if ($capture[$expectation.Key] -ne $expectation.Value) {
            throw (
                "cycle $cycle trace $($expectation.Key) is " +
                "'$($capture[$expectation.Key])'"
            )
        }
    }
    if ([int]$capture.module_notifications -lt 6) {
        throw "cycle $cycle trace lost module transition records"
    }
    if ((Test-Path -LiteralPath "$report.tmp") -or
        (Test-Path -LiteralPath "$trace.tmp")) {
        throw "cycle $cycle left an incomplete atomic artifact"
    }

    Assert-ProcessExited `
        -ProcessId ([int]$finalization.process_id) `
        -Role "retail"
    Assert-ProcessExited `
        -ProcessId ([int]$finalization.collector_id) `
        -Role "collector"

    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
    $lastCycleHandles = Get-SelfHandleCount
    if ($cycle -eq 1) {
        $firstCycleHandles = $lastCycleHandles
    }
}

if ($lastCycleHandles -gt $firstCycleHandles + 2) {
    throw (
        "soak coordinator leaked handles: first=$firstCycleHandles " +
        "last=$lastCycleHandles"
    )
}
$reports = @(
    Get-ChildItem -LiteralPath $OutputDir -Filter "*-finalization.txt" -File
)
$traces = @(
    Get-ChildItem -LiteralPath $OutputDir -Filter "*-trace.txt" -File
)
if ($reports.Count -ne $Cycles -or $traces.Count -ne $Cycles) {
    throw (
        "soak artifact count mismatch: reports=$($reports.Count) " +
        "traces=$($traces.Count) cycles=$Cycles"
    )
}

Write-Host (
    "retail-soak-v1 state=complete cycles=$Cycles " +
    "handle_first=$firstCycleHandles handle_last=$lastCycleHandles " +
    "reports=$($reports.Count) traces=$($traces.Count)"
)
