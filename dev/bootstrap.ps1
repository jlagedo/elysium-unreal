[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$LockPath = Join-Path $RepoRoot "dev/dependencies.lock.json"
$Lock = Get-Content -Raw -LiteralPath $LockPath | ConvertFrom-Json

function Assert-ManagedDestination([string]$Path) {
    $externalRoot = [IO.Path]::GetFullPath((Join-Path $RepoRoot "Plugins/External"))
    $resolved = [IO.Path]::GetFullPath($Path)
    if (!$resolved.StartsWith($externalRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "refusing to manage dependency outside $externalRoot`: $resolved"
    }
}

function Invoke-Git([string]$WorkingDirectory, [string[]]$Arguments) {
    & git -C $WorkingDirectory @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed in $WorkingDirectory"
    }
}

function Get-RelativePath([string]$BaseDirectory, [string]$Path) {
    $base = [IO.Path]::GetFullPath($BaseDirectory).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($Path)
    if (!$resolved.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
        throw "path is outside managed dependency root: $resolved"
    }
    return $resolved.Substring($base.Length)
}

function Get-ManagedContentHash([string]$Directory) {
    [string[]]$lines = Get-ChildItem -LiteralPath $Directory -Recurse -File |
        Where-Object {
            $_.Name -ne ".elysium-managed.json" -and
            (Get-RelativePath $Directory $_.FullName) -notmatch
                "^(Binaries|Intermediate)[\\/]"
        } |
        ForEach-Object {
            $relative = (Get-RelativePath $Directory $_.FullName).Replace("\", "/")
            "$relative=$((Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant())"
        }
    [Array]::Sort($lines, [StringComparer]::Ordinal)
    $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace "-", "").ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot "Plugins/External") | Out-Null

foreach ($plugin in $Lock.plugins) {
    $destination = [IO.Path]::GetFullPath((Join-Path $RepoRoot $plugin.destination))
    $staging = $destination + ".__fetch"
    $markerPath = Join-Path $destination ".elysium-managed.json"
    Assert-ManagedDestination $destination
    Assert-ManagedDestination $staging

    if (Test-Path -LiteralPath $markerPath) {
        $marker = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json
        $treeMatches = !$plugin.post_patch_tree -or $marker.post_patch_tree -eq $plugin.post_patch_tree
        $contentMatches = $marker.content_hash -and
            $marker.content_hash -eq (Get-ManagedContentHash $destination)
        if ($marker.revision -eq $plugin.revision -and $treeMatches -and $contentMatches) {
            Write-Host "[bootstrap] $($plugin.name) already matches the lock"
            continue
        }
    }

    if (Test-Path -LiteralPath $destination) {
        Assert-ManagedDestination $destination
        Remove-Item -Recurse -Force -LiteralPath $destination
    }
    if (Test-Path -LiteralPath $staging) {
        Assert-ManagedDestination $staging
        Remove-Item -Recurse -Force -LiteralPath $staging
    }

    Write-Host "[bootstrap] fetching $($plugin.name) at $($plugin.revision)"
    New-Item -ItemType Directory -Force -Path $staging | Out-Null
    Invoke-Git $staging @("init", "--quiet")
    Invoke-Git $staging @("remote", "add", "origin", $plugin.repository)
    Invoke-Git $staging @("fetch", "--quiet", "--depth", "1", "origin", $plugin.revision)
    Invoke-Git $staging @("checkout", "--quiet", "--detach", "FETCH_HEAD")

    if ($plugin.patch) {
        $patchPath = [IO.Path]::GetFullPath((Join-Path $RepoRoot $plugin.patch))
        Invoke-Git $staging @("apply", "--whitespace=nowarn", $patchPath)
    }

    Invoke-Git $staging @("add", "-A")
    $rootTree = (& git -C $staging write-tree).Trim()
    $sourceSubdirectory = if ($plugin.source_subdirectory) {
        [string]$plugin.source_subdirectory
    } else {
        "."
    }
    $tree = if ($sourceSubdirectory -eq ".") {
        $rootTree
    } else {
        (& git -C $staging rev-parse "$rootTree`:$sourceSubdirectory").Trim()
    }
    if ($LASTEXITCODE -ne 0 -or !$tree) {
        throw "could not compute the post-patch tree for $($plugin.name)"
    }
    if ($plugin.post_patch_tree -and $tree -ne $plugin.post_patch_tree) {
        throw "$($plugin.name) post-patch tree mismatch: got $tree, expected $($plugin.post_patch_tree)"
    }

    if ($sourceSubdirectory -eq ".") {
        $gitDir = Join-Path $staging ".git"
        if (Test-Path -LiteralPath $gitDir) {
            Remove-Item -Recurse -Force -LiteralPath $gitDir
        }
        Move-Item -LiteralPath $staging -Destination $destination
    } else {
        $sourcePath = Join-Path $staging $sourceSubdirectory
        if (!(Test-Path -LiteralPath $sourcePath -PathType Container)) {
            throw "$($plugin.name) source subdirectory is missing: $sourceSubdirectory"
        }
        Move-Item -LiteralPath $sourcePath -Destination $destination
        Remove-Item -Recurse -Force -LiteralPath $staging
    }
    $contentHash = Get-ManagedContentHash $destination
    @{
        name = $plugin.name
        repository = $plugin.repository
        revision = $plugin.revision
        post_patch_tree = $tree
        content_hash = $contentHash
    } | ConvertTo-Json | Set-Content -Encoding utf8 -LiteralPath $markerPath
    Write-Host "[bootstrap] $($plugin.name) ready (tree $tree, content $contentHash)"
}

Write-Host "[bootstrap] external plugins are ready"
