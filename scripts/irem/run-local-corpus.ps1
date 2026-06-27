#!/usr/bin/env pwsh
# Wire the current local Irem corpus layout into the data-gated test runner.
#
# This helper does not copy or package ROM data. It only points the existing
# data-gated tests at a caller-owned corpus root. The full M72 roster remains
# opt-in because incomplete local media should fail the strict roster golden.

param(
    [string]$Root = "D:\emu\irem",
    [string]$BuildDir = "build/windows-msvc-debug",
    [switch]$IncludeFullM72Roster
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runner = Join-Path $repoRoot "scripts\run-data-gated-tests.ps1"
$m72ArtifactScanner = Join-Path $repoRoot "scripts\irem_m72\find-missing-artifacts.ps1"
if (-not (Test-Path $runner)) {
    throw "Data-gated test runner not found: $runner"
}

if (-not (Test-Path $Root)) {
    throw "Irem corpus root not found: $Root"
}

function Join-CorpusPath {
    param(
        [Parameter(Mandatory = $true)][string]$Base,
        [Parameter(Mandatory = $true)][string]$Child
    )
    return [System.IO.Path]::Combine($Base, $Child)
}

function First-ExistingPath {
    param([Parameter(Mandatory = $true)][string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Set-EnvIfPathExists {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (Test-Path $Path) {
        $resolved = (Resolve-Path -LiteralPath $Path).Path
        [Environment]::SetEnvironmentVariable($Name, $resolved, "Process")
        Write-Host ("  [wired] {0,-30} {1}" -f $Name, $resolved) -ForegroundColor Green
        return
    }

    [Environment]::SetEnvironmentVariable($Name, $null, "Process")
    Write-Host ("  [skip]  {0,-30} missing: {1}" -f $Name, $Path) -ForegroundColor DarkGray
}

function Set-EnvIfPathListExists {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$Paths
    )

    $existing = [System.Collections.Generic.List[string]]::new()
    foreach ($path in $Paths) {
        if (Test-Path $path) {
            $existing.Add((Resolve-Path -LiteralPath $path).Path)
        }
    }

    if ($existing.Count -eq 0) {
        [Environment]::SetEnvironmentVariable($Name, $null, "Process")
        Write-Host ("  [skip]  {0,-30} no existing roots" -f $Name) -ForegroundColor DarkGray
        return
    }

    $value = [string]::Join([System.IO.Path]::PathSeparator, $existing)
    [Environment]::SetEnvironmentVariable($Name, $value, "Process")
    Write-Host ("  [wired] {0,-30} {1}" -f $Name, $value) -ForegroundColor Green
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path
$m72Root = Join-CorpusPath -Base $rootPath -Child "M72"
$m72ArtifactPreflightExitCode = 0

Write-Host "Wiring local Irem corpus from $rootPath" -ForegroundColor Cyan

Set-EnvIfPathExists -Name "MNEMOS_M14_SET_DIR" -Path $rootPath
Set-EnvIfPathExists -Name "MNEMOS_M15_SET_DIR" -Path (Join-CorpusPath -Base $rootPath -Child "M15")
Set-EnvIfPathExists -Name "MNEMOS_M52_SET_DIR" -Path $rootPath
Set-EnvIfPathExists -Name "MNEMOS_M62_SET_DIR" -Path $rootPath
Set-EnvIfPathExists -Name "MNEMOS_M63_SET_DIR" -Path $rootPath
Set-EnvIfPathExists -Name "MNEMOS_M75_SET_DIR" -Path $rootPath
Set-EnvIfPathExists -Name "MNEMOS_M81_SET_DIR" -Path (Join-CorpusPath -Base $rootPath -Child "M81")
Set-EnvIfPathExists -Name "MNEMOS_M82_SET_DIR" -Path $rootPath
Set-EnvIfPathListExists -Name "MNEMOS_M84_SET_DIR" -Paths @(
    (Join-CorpusPath -Base $rootPath -Child "M84"),
    (Join-CorpusPath -Base $rootPath -Child "M81"),
    $m72Root
)
Set-EnvIfPathExists -Name "MNEMOS_M90_SET_DIR" -Path $rootPath
Set-EnvIfPathExists -Name "MNEMOS_M92_SET_DIR" -Path $rootPath
Set-EnvIfPathExists -Name "MNEMOS_M107_SET_DIR" -Path (Join-CorpusPath -Base $rootPath -Child "M107")

$rtypeSet = First-ExistingPath @(
    (Join-CorpusPath -Base $m72Root -Child "rtype.zip"),
    (Join-CorpusPath -Base $rootPath -Child "R-Type_Arcade_EN.zip")
)
$protectedSet = First-ExistingPath @(
    (Join-CorpusPath -Base $rootPath -Child "imgfight.zip"),
    (Join-CorpusPath -Base $m72Root -Child "imgfight.zip")
)
$protectedAudioSet = First-ExistingPath @(
    (Join-CorpusPath -Base $m72Root -Child "dbreedm72"),
    (Join-CorpusPath -Base $rootPath -Child "dbreedm72")
)
$protectedMcuSet = First-ExistingPath @(
    (Join-CorpusPath -Base $m72Root -Child "nspirit.zip"),
    (Join-CorpusPath -Base $rootPath -Child "Ninja-Spirit_Arcade_EN.zip")
)

if ($null -ne $rtypeSet) {
    [Environment]::SetEnvironmentVariable("MNEMOS_M72_RTYPE_SET", $rtypeSet, "Process")
}
if ($null -ne $protectedSet) {
    [Environment]::SetEnvironmentVariable("MNEMOS_M72_PROTECTED_SET", $protectedSet, "Process")
    [Environment]::SetEnvironmentVariable("MNEMOS_M72_VERTICAL_SET", $protectedSet, "Process")
}
if ($null -ne $protectedAudioSet) {
    [Environment]::SetEnvironmentVariable("MNEMOS_M72_PROTECTED_AUDIO_SET", $protectedAudioSet, "Process")
}
if ($null -ne $protectedMcuSet) {
    [Environment]::SetEnvironmentVariable("MNEMOS_M72_PROTECTED_MCU_SET", $protectedMcuSet, "Process")
}

foreach ($name in @(
        "MNEMOS_M72_RTYPE_SET",
        "MNEMOS_M72_PROTECTED_SET",
        "MNEMOS_M72_PROTECTED_AUDIO_SET",
        "MNEMOS_M72_PROTECTED_MCU_SET",
        "MNEMOS_M72_VERTICAL_SET"
    )) {
    $value = [Environment]::GetEnvironmentVariable($name, "Process")
    if ([string]::IsNullOrWhiteSpace($value)) {
        Write-Host ("  [skip]  {0,-30} no matching local path" -f $name) -ForegroundColor DarkGray
    } else {
        Write-Host ("  [wired] {0,-30} {1}" -f $name, $value) -ForegroundColor Green
    }
}

if ($IncludeFullM72Roster) {
    Set-EnvIfPathExists -Name "MNEMOS_M72_SET_DIR" -Path $rootPath
    if (Test-Path $m72ArtifactScanner) {
        Write-Host ""
        Write-Host "Running strict M72 artifact preflight from checked-in manifests..." -ForegroundColor Cyan
        & $m72ArtifactScanner -Root $rootPath -Recurse -ScanAllSevenZipEntries
        $m72ArtifactPreflightExitCode = $LASTEXITCODE
        if ($m72ArtifactPreflightExitCode -ne 0) {
            Write-Host "M72 artifact preflight found missing required files; continuing to CTest for the existing roster gate details." -ForegroundColor Yellow
        }
    } else {
        Write-Host "  [skip]  M72 artifact preflight       scanner missing: $m72ArtifactScanner" -ForegroundColor DarkGray
    }
} else {
    [Environment]::SetEnvironmentVariable("MNEMOS_M72_SET_DIR", $null, "Process")
    Write-Host "  [skip]  MNEMOS_M72_SET_DIR             full roster gate omitted; pass -IncludeFullM72Roster to require it" -ForegroundColor DarkGray
}

& $runner -BuildDir $BuildDir
$runnerExitCode = $LASTEXITCODE
if ($runnerExitCode -ne 0) {
    exit $runnerExitCode
}
exit $m72ArtifactPreflightExitCode
