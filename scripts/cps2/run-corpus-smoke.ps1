#!/usr/bin/env pwsh
# Data-gated CPS2 corpus smoke runner.
#
# ROM/key zips are never committed. Point this at a single zip with
# MNEMOS_CPS2_ROM, or at a directory of zips with MNEMOS_CPS2_SET_DIR.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string]$Rom = "",
    [string]$RomDir = "",
    [int]$Frames = 20,
    [int]$MaxSets = 0,
    [switch]$IncludeAllZips,
    [switch]$Recurse
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Add-RomPath {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Paths,
        [string]$Path
    )
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }
    $resolved = Resolve-RepoPath $Path
    if (Test-Path -LiteralPath $resolved -PathType Leaf) {
        $Paths.Add((Resolve-Path -LiteralPath $resolved).Path)
    } else {
        Write-Warning "CPS2 ROM path not found: $Path"
    }
}

function Invoke-Player {
    param(
        [Parameter(Mandatory = $true)][string]$Player,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    & $Player @Arguments *> $LogPath
    return $LASTEXITCODE
}

function Get-Cps2ManifestSetIds {
    $gamesDir = Join-Path $repoRoot "src/manifests/capcom_cps2/games"
    $ids = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    if (Test-Path -LiteralPath $gamesDir -PathType Container) {
        foreach ($toml in Get-ChildItem -LiteralPath $gamesDir -Filter "*.toml" -File) {
            [void]$ids.Add([System.IO.Path]::GetFileNameWithoutExtension($toml.Name))
        }
    }
    return $ids
}

function Test-Cps2ZipCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$ManifestIds
    )
    if ($IncludeAllZips) {
        return $true
    }
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    if ($ManifestIds.Contains($stem)) {
        return $true
    }
    $underscore = $stem.IndexOf("_", [System.StringComparison]::Ordinal)
    if ($underscore -gt 0) {
        return $ManifestIds.Contains($stem.Substring(0, $underscore))
    }
    return $false
}

if ([string]::IsNullOrWhiteSpace($Rom)) {
    $Rom = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_ROM")
}
if ([string]::IsNullOrWhiteSpace($RomDir)) {
    $RomDir = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_SET_DIR")
}

$buildRoot = Resolve-RepoPath $BuildDir
$player = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "mnemos_player.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($player)) {
    throw "mnemos_player.exe not found under '$buildRoot'. Build mnemos_player first."
}

$roms = [System.Collections.Generic.List[string]]::new()
Add-RomPath -Paths $roms -Path $Rom

$manifestIds = Get-Cps2ManifestSetIds
if (-not [string]::IsNullOrWhiteSpace($RomDir)) {
    $resolvedDir = Resolve-RepoPath $RomDir
    if (Test-Path -LiteralPath $resolvedDir -PathType Container) {
        $childArgs = @{
            LiteralPath = $resolvedDir
            Filter = "*.zip"
            File = $true
            ErrorAction = "SilentlyContinue"
        }
        if ($Recurse) {
            $childArgs.Recurse = $true
        }
        foreach ($zip in Get-ChildItem @childArgs | Sort-Object FullName) {
            if (Test-Cps2ZipCandidate -Path $zip.FullName -ManifestIds $manifestIds) {
                $roms.Add($zip.FullName)
            }
        }
    } else {
        Write-Warning "CPS2 ROM directory not found: $RomDir"
    }
}

$extra = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_EXTRA_ROMS")
if (-not [string]::IsNullOrWhiteSpace($extra)) {
    foreach ($path in $extra.Split([System.IO.Path]::PathSeparator, [System.StringSplitOptions]::RemoveEmptyEntries)) {
        Add-RomPath -Paths $roms -Path $path
    }
}

$uniqueRoms = @($roms | Sort-Object -Unique)
if ($MaxSets -gt 0) {
    $uniqueRoms = @($uniqueRoms | Select-Object -First $MaxSets)
}

if ($uniqueRoms.Count -eq 0) {
    Write-Host "No CPS2 ROMs configured; set MNEMOS_CPS2_ROM or MNEMOS_CPS2_SET_DIR to run this gate." -ForegroundColor DarkGray
    exit 0
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outDir = Join-Path $repoRoot "build/scratch/cps2-corpus/$stamp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
$index = 0
foreach ($romPath in $uniqueRoms) {
    $index += 1
    $setId = [System.IO.Path]::GetFileNameWithoutExtension($romPath)
    $setOut = Join-Path $outDir ("{0:D3}-{1}" -f $index, $setId)
    New-Item -ItemType Directory -Force -Path $setOut | Out-Null

    $statePath = Join-Path $setOut "$setId.mns"
    $saveLog = Join-Path $setOut "$setId.save.log"
    $loadLog = Join-Path $setOut "$setId.load.log"
    $screenshotPath = Join-Path $setOut "$setId.after-load.ppm"

    Write-Host ("[cps2] {0}" -f $setId) -ForegroundColor Cyan

    $saveArgs = @(
        "--system", "cps2",
        "--rom", $romPath,
        "--frames", $Frames.ToString(),
        "--press", "start@1+2",
        "--press", "service@2+2",
        "--press", "test@3+2",
        "--press", "paddle=0x123@4+2",
        "--save-state", $statePath
    )
    $saveExit = Invoke-Player -Player $player -LogPath $saveLog -Arguments $saveArgs

    $loadExit = $null
    if ($saveExit -eq 0) {
        $loadArgs = @(
            "--system", "cps2",
            "--rom", $romPath,
            "--load-state", $statePath,
            "--frames", "1",
            "--screenshot", $screenshotPath
        )
        $loadExit = Invoke-Player -Player $player -LogPath $loadLog -Arguments $loadArgs
    }

    $passed = ($saveExit -eq 0 -and $loadExit -eq 0 -and (Test-Path -LiteralPath $statePath) -and
        (Test-Path -LiteralPath $screenshotPath))
    $results.Add([pscustomobject]@{
        set = $setId
        rom = $romPath
        save_exit = $saveExit
        load_exit = $loadExit
        passed = $passed
        state = $statePath
        screenshot = $screenshotPath
        save_log = $saveLog
        load_log = $loadLog
    })
}

$summaryPath = Join-Path $outDir "summary.json"
$results | ConvertTo-Json -Depth 4 | Set-Content -Path $summaryPath -Encoding utf8

$failed = @($results | Where-Object { -not $_.passed })
Write-Host ("CPS2 corpus smoke: {0}/{1} passed; summary: {2}" -f ($results.Count - $failed.Count), $results.Count, $summaryPath)
if ($failed.Count -gt 0) {
    foreach ($row in $failed) {
        Write-Host ("  [fail] {0} save={1} load={2}" -f $row.set, $row.save_exit, $row.load_exit) -ForegroundColor Red
    }
    exit 1
}

exit 0
