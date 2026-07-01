#!/usr/bin/env pwsh
# Data-gated Amiga ADF/ADZ/ZIP player smoke runner.
#
# Kickstart ROMs and Amiga software are never committed. Point this at one or
# more disk images with -Rom, or at local corpus directories with -RomDir,
# MNEMOS_AMIGA_ROM, MNEMOS_AMIGA_ADF_DIR, or MNEMOS_AMIGA_SET_DIR. Kickstart
# can come from per-model parameters/env vars or from -BiosDir /
# MNEMOS_AMIGA_BIOS_DIR.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string[]]$Rom = @(),
    [string[]]$RomDir = @(),
    [ValidateSet("amiga500", "amiga500plus", "amiga600")]
    [string[]]$System = @("amiga500"),
    [string]$Kickstart500 = $env:MNEMOS_AMIGA500_KICKSTART,
    [string]$Kickstart500Plus = $env:MNEMOS_AMIGA500PLUS_KICKSTART,
    [string]$Kickstart600 = $env:MNEMOS_AMIGA600_KICKSTART,
    [string]$BiosDir = $env:MNEMOS_AMIGA_BIOS_DIR,
    [string]$KickstartDir = $env:MNEMOS_AMIGA_KICKSTART_DIR,
    [int]$Frames = 120,
    [int]$MaxSets = 0,
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

function Split-PathList {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }
    return @($Value.Split([System.IO.Path]::PathSeparator, [System.StringSplitOptions]::RemoveEmptyEntries) |
        ForEach-Object { $_.Trim() } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Add-MediaPath {
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
        Write-Warning "Amiga media path not found: $Path"
    }
}

function Test-AmigaDirectoryMediaCandidate {
    param([Parameter(Mandatory = $true)][string]$Path)
    $extension = [System.IO.Path]::GetExtension($Path)
    if ($extension.Equals(".adf", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".adz", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    if ($Path.EndsWith(".adf.gz", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    if (-not $extension.Equals(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $entries = & tar -tf $Path 2>$null
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Could not inspect ZIP archive while scanning Amiga media: $Path"
            return $false
        }
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    foreach ($entry in $entries) {
        $entryExtension = [System.IO.Path]::GetExtension($entry)
        if ($entryExtension.Equals(".adf", [System.StringComparison]::OrdinalIgnoreCase) -or
            $entryExtension.Equals(".adz", [System.StringComparison]::OrdinalIgnoreCase) -or
            $entryExtension.Equals(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Add-MediaDir {
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
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        Write-Warning "Amiga media directory not found: $Path"
        return
    }

    $extensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($extension in @(".adf", ".adz", ".gz", ".zip")) {
        [void]$extensions.Add($extension)
    }

    foreach ($file in Get-ChildItem -LiteralPath $resolved -File -Recurse:$Recurse) {
        if ($extensions.Contains($file.Extension) -and
            (Test-AmigaDirectoryMediaCandidate -Path $file.FullName)) {
            $Paths.Add($file.FullName)
        }
    }
}

function Get-SafeName {
    param([Parameter(Mandatory = $true)][string]$Path)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $safe = [System.Text.RegularExpressions.Regex]::Replace($stem, "[^A-Za-z0-9._-]+", "_")
    $safe = $safe.Trim("_")
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "media"
    }
    return $safe
}

function Get-KickstartConfig {
    param([Parameter(Mandatory = $true)][string]$SystemName)
    switch ($SystemName) {
        "amiga500" {
            return [pscustomobject]@{
                EnvVar = "MNEMOS_AMIGA500_KICKSTART"
                Path = $Kickstart500
            }
        }
        "amiga500plus" {
            return [pscustomobject]@{
                EnvVar = "MNEMOS_AMIGA500PLUS_KICKSTART"
                Path = $Kickstart500Plus
            }
        }
        "amiga600" {
            return [pscustomobject]@{
                EnvVar = "MNEMOS_AMIGA600_KICKSTART"
                Path = $Kickstart600
            }
        }
    }
}

function Invoke-Player {
    param(
        [Parameter(Mandatory = $true)][string]$Player,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $previousErrorActionPreference = $ErrorActionPreference
    $hasNativeErrorPreference =
        $null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)
    if ($hasNativeErrorPreference) {
        $previousNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
    }
    try {
        $ErrorActionPreference = "Continue"
        & $Player @Arguments *> $LogPath
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
        if ($hasNativeErrorPreference) {
            $PSNativeCommandUseErrorActionPreference = $previousNativeErrorPreference
        }
    }
}

function Read-PpmToken {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][ref]$Offset
    )
    while ($Offset.Value -lt $Bytes.Length) {
        $b = $Bytes[$Offset.Value]
        if ($b -eq 35) {
            while ($Offset.Value -lt $Bytes.Length -and $Bytes[$Offset.Value] -ne 10) {
                ++$Offset.Value
            }
            continue
        }
        if ($b -eq 9 -or $b -eq 10 -or $b -eq 13 -or $b -eq 32) {
            ++$Offset.Value
            continue
        }
        break
    }

    $start = $Offset.Value
    while ($Offset.Value -lt $Bytes.Length) {
        $b = $Bytes[$Offset.Value]
        if ($b -eq 9 -or $b -eq 10 -or $b -eq 13 -or $b -eq 32) {
            break
        }
        ++$Offset.Value
    }
    if ($Offset.Value -le $start) {
        throw "Malformed PPM header."
    }
    return [System.Text.Encoding]::ASCII.GetString($Bytes, $start, $Offset.Value - $start)
}

function Get-PpmStats {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Screenshot not written: $Path"
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    $offsetValue = 0
    $offset = [ref]$offsetValue
    $magic = Read-PpmToken -Bytes $bytes -Offset $offset
    if ($magic -ne "P6") {
        throw "Expected P6 PPM screenshot, got '$magic'."
    }
    $width = [int](Read-PpmToken -Bytes $bytes -Offset $offset)
    $height = [int](Read-PpmToken -Bytes $bytes -Offset $offset)
    $maxValue = [int](Read-PpmToken -Bytes $bytes -Offset $offset)
    if ($width -le 0 -or $height -le 0 -or $maxValue -ne 255) {
        throw "Unsupported PPM header in $Path."
    }

    while ($offset.Value -lt $bytes.Length) {
        $b = $bytes[$offset.Value]
        if ($b -ne 9 -and $b -ne 10 -and $b -ne 13 -and $b -ne 32) {
            break
        }
        ++$offset.Value
    }

    $pixels = $width * $height
    $needed = $offset.Value + ($pixels * 3)
    if ($bytes.Length -lt $needed) {
        throw "Truncated PPM data in $Path."
    }

    $nonBlack = 0
    for ($i = $offset.Value; $i -lt $needed; $i += 3) {
        if ($bytes[$i] -ne 0 -or $bytes[$i + 1] -ne 0 -or $bytes[$i + 2] -ne 0) {
            ++$nonBlack
        }
    }

    return [pscustomobject]@{
        Width = $width
        Height = $height
        Pixels = $pixels
        NonBlackPixels = $nonBlack
    }
}

if ($Frames -le 0) {
    throw "-Frames must be positive."
}
if ($MaxSets -lt 0) {
    throw "-MaxSets cannot be negative."
}

$buildPath = Resolve-RepoPath $BuildDir
$player = Join-Path $buildPath "src/apps/player/mnemos_player.exe"
if (-not (Test-Path -LiteralPath $player -PathType Leaf)) {
    throw "Player executable not found at '$player'. Build mnemos_player first, or pass -BuildDir."
}

$media = [System.Collections.Generic.List[string]]::new()
foreach ($path in $Rom) {
    Add-MediaPath -Paths $media -Path $path
}
foreach ($path in Split-PathList $env:MNEMOS_AMIGA_ROM) {
    Add-MediaPath -Paths $media -Path $path
}
foreach ($dir in $RomDir) {
    Add-MediaDir -Paths $media -Path $dir
}
foreach ($dir in Split-PathList $env:MNEMOS_AMIGA_ADF_DIR) {
    Add-MediaDir -Paths $media -Path $dir
}
foreach ($dir in Split-PathList $env:MNEMOS_AMIGA_SET_DIR) {
    Add-MediaDir -Paths $media -Path $dir
}

$deduped = [System.Collections.Generic.List[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($path in $media) {
    if ($seen.Add($path)) {
        $deduped.Add($path)
    }
}
if ($MaxSets -gt 0 -and $deduped.Count -gt $MaxSets) {
    $deduped = [System.Collections.Generic.List[string]]::new($deduped.GetRange(0, $MaxSets))
}
if ($deduped.Count -eq 0) {
    throw "No Amiga media found. Pass -Rom/-RomDir or set MNEMOS_AMIGA_ROM, MNEMOS_AMIGA_ADF_DIR, or MNEMOS_AMIGA_SET_DIR."
}

$artifactDir = Resolve-RepoPath "build/scratch/amiga-corpus"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
$failures = [System.Collections.Generic.List[string]]::new()

$previousBiosDir = [Environment]::GetEnvironmentVariable("MNEMOS_AMIGA_BIOS_DIR")
$previousKickstartDir = [Environment]::GetEnvironmentVariable("MNEMOS_AMIGA_KICKSTART_DIR")
if (-not [string]::IsNullOrWhiteSpace($BiosDir)) {
    if (-not (Test-Path -LiteralPath $BiosDir -PathType Container)) {
        throw "MNEMOS_AMIGA_BIOS_DIR points to a missing directory: $BiosDir"
    }
    [Environment]::SetEnvironmentVariable(
        "MNEMOS_AMIGA_BIOS_DIR", (Resolve-Path -LiteralPath $BiosDir).Path, "Process")
}
if (-not [string]::IsNullOrWhiteSpace($KickstartDir)) {
    if (-not (Test-Path -LiteralPath $KickstartDir -PathType Container)) {
        throw "MNEMOS_AMIGA_KICKSTART_DIR points to a missing directory: $KickstartDir"
    }
    [Environment]::SetEnvironmentVariable(
        "MNEMOS_AMIGA_KICKSTART_DIR", (Resolve-Path -LiteralPath $KickstartDir).Path, "Process")
}

try {
    foreach ($systemName in $System) {
        $kickstart = Get-KickstartConfig -SystemName $systemName
        $hasSharedKickstartDir =
            -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable("MNEMOS_AMIGA_BIOS_DIR")) -or
            -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable("MNEMOS_AMIGA_KICKSTART_DIR"))
        if ([string]::IsNullOrWhiteSpace($kickstart.Path) -and -not $hasSharedKickstartDir) {
            throw "$($kickstart.EnvVar), MNEMOS_AMIGA_BIOS_DIR, or MNEMOS_AMIGA_KICKSTART_DIR is not configured for $systemName."
        }
        if (-not [string]::IsNullOrWhiteSpace($kickstart.Path) -and
            -not (Test-Path -LiteralPath $kickstart.Path -PathType Leaf)) {
            throw "$($kickstart.EnvVar) points to a missing file: $($kickstart.Path)"
        }

        $previousKickstart = [Environment]::GetEnvironmentVariable($kickstart.EnvVar)
        if (-not [string]::IsNullOrWhiteSpace($kickstart.Path)) {
            [Environment]::SetEnvironmentVariable($kickstart.EnvVar, (Resolve-Path -LiteralPath $kickstart.Path).Path, "Process")
        } else {
            [Environment]::SetEnvironmentVariable($kickstart.EnvVar, $null, "Process")
        }
        try {
            foreach ($path in $deduped) {
                $safeName = "{0}-{1}" -f $systemName, (Get-SafeName -Path $path)
                $screenshot = Join-Path $artifactDir "$safeName.ppm"
                $log = Join-Path $artifactDir "$safeName.log"
                $args = @(
                    "--system", $systemName,
                    "--rom", $path,
                    "--frames", $Frames.ToString([System.Globalization.CultureInfo]::InvariantCulture),
                    "--screenshot", $screenshot
                )

                $exitCode = Invoke-Player -Player $player -LogPath $log -Arguments $args
                if ($exitCode -ne 0) {
                    $failures.Add("$systemName failed for '$path' with exit code $exitCode. See $log")
                    continue
                }

                try {
                    $stats = Get-PpmStats -Path $screenshot
                    if ($stats.NonBlackPixels -eq 0) {
                        $failures.Add("$systemName produced an all-black frame for '$path'. See $screenshot")
                    }
                    $results.Add([pscustomobject]@{
                        System = $systemName
                        Media = $path
                        Frames = $Frames
                        Width = $stats.Width
                        Height = $stats.Height
                        NonBlackPixels = $stats.NonBlackPixels
                        Screenshot = $screenshot
                        Log = $log
                    })
                } catch {
                    $failures.Add("$systemName could not validate screenshot for '$path': $($_.Exception.Message)")
                }
            }
        } finally {
            [Environment]::SetEnvironmentVariable($kickstart.EnvVar, $previousKickstart, "Process")
        }
    }
} finally {
    [Environment]::SetEnvironmentVariable("MNEMOS_AMIGA_BIOS_DIR", $previousBiosDir, "Process")
    [Environment]::SetEnvironmentVariable("MNEMOS_AMIGA_KICKSTART_DIR", $previousKickstartDir, "Process")
}

$results | Format-Table -AutoSize System, Frames, Width, Height, NonBlackPixels, Media

if ($failures.Count -gt 0) {
    throw "Amiga corpus smoke failed:`n$($failures -join "`n")"
}

Write-Host "Amiga corpus smoke passed for $($results.Count) launch(es). Artifacts: $artifactDir"
