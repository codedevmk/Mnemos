#!/usr/bin/env pwsh
# Data-gated Amiga ADF/ADZ/IPF/HDF/archive player smoke runner.
# Use -ContentProbeFrames with comma-separated frame counts to capture multiple
# screenshot candidates and keep the most useful one in the summary. Use -Press
# to pass scripted input through every screenshot candidate run.
#
# Kickstart ROMs and Amiga software are never committed. Point this at one or
# more disk or hard-drive images with -Rom, or at local corpus directories with -RomDir,
# MNEMOS_AMIGA_ROM, MNEMOS_AMIGA_ADF_DIR, or MNEMOS_AMIGA_SET_DIR. Kickstart
# can come from per-model parameters/env vars or from -BiosDir /
# MNEMOS_AMIGA_BIOS_DIR.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string[]]$Rom = @(),
    [string[]]$RomDir = @(),
    [string[]]$System = @("amiga500"),
    [string]$Kickstart1000 = $env:MNEMOS_AMIGA1000_KICKSTART,
    [string]$Kickstart500 = $env:MNEMOS_AMIGA500_KICKSTART,
    [string]$Kickstart500Plus = $env:MNEMOS_AMIGA500PLUS_KICKSTART,
    [string]$Kickstart600 = $env:MNEMOS_AMIGA600_KICKSTART,
    [string]$Kickstart2000 = $env:MNEMOS_AMIGA2000_KICKSTART,
    [string]$BiosDir = $env:MNEMOS_AMIGA_BIOS_DIR,
    [string]$KickstartDir = $env:MNEMOS_AMIGA_KICKSTART_DIR,
    [int]$Frames = 120,
    [string[]]$ContentProbeFrames = @(),
    [string[]]$Press = @(),
    [int]$MaxSets = 0,
    [string]$StartAfter = "",
    [double]$MinimumHeadlessFps = 0.0,
    [switch]$RejectFlatFrame,
    [switch]$AllowBlackFrame,
    [int]$MinimumUniqueColors = 0,
    [double]$MaximumDominantColorRatio = 0.0,
    [switch]$RequireDiskProgress,
    [int]$MinimumDiskCylinder = -1,
    [switch]$RejectKickstartPrompt,
    [switch]$RejectSoftwareFailure,
    [switch]$RequireRenderedAudio,
    [int]$AudioFrames = 0,
    [string[]]$AudioPress = @(),
    [int]$MinimumAudioFramesWithSignal = 1,
    [int]$MinimumAudioPeakAbs = 1,
    [string]$ExpectedSummary = "",
    [switch]$RequireExpectedRows,
    [switch]$ListSets,
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

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "File not written: $Path"
    }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}

function Get-OptionalText {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return ""
    }
    return [string]$property.Value
}

function Get-SummaryRowKey {
    param([Parameter(Mandatory = $true)]$Row)
    $kickstart = Get-OptionalText -Object $Row -Name "KickstartPath"
    if ([string]::IsNullOrWhiteSpace($kickstart)) {
        $kickstart = Get-OptionalText -Object $Row -Name "Kickstart"
    }
    if ([string]::IsNullOrWhiteSpace($kickstart)) {
        $kickstart = "<unspecified-kickstart>"
    }
    return "{0}|{1}|{2}|{3}|{4}" -f $Row.System, $Row.MediaLabel, $Row.MediaCount, $kickstart, $Row.Frames
}

function Read-ExpectedSummaryRows {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "-ExpectedSummary points to a missing file: $Path"
    }
    $extension = [System.IO.Path]::GetExtension($resolved)
    if ($extension.Equals(".json", [System.StringComparison]::OrdinalIgnoreCase)) {
        $json = Get-Content -Raw -LiteralPath $resolved | ConvertFrom-Json
        return @($json)
    }
    return @(Import-Csv -LiteralPath $resolved)
}

function Add-ExpectedSummaryFailures {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[object]]$Results,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][bool]$RequireRows,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Failures
    )

    $expectedRows = Read-ExpectedSummaryRows -Path $Path
    $expectedByKey = @{}
    foreach ($row in $expectedRows) {
        $key = Get-SummaryRowKey -Row $row
        if ($expectedByKey.ContainsKey($key)) {
            $Failures.Add("Expected summary has duplicate row key '$key'.")
            continue
        }
        $expectedByKey[$key] = $row
    }

    foreach ($current in $Results) {
        $key = Get-SummaryRowKey -Row $current
        if (-not $expectedByKey.ContainsKey($key)) {
            if ($RequireRows) {
                $Failures.Add("Expected summary has no row for '$key'.")
            }
            continue
        }

        $expected = $expectedByKey[$key]
        foreach ($field in @("ScreenshotSha256", "AudioWavSha256", "AudioTraceSha256")) {
            $expectedValue = Get-OptionalText -Object $expected -Name $field
            if ([string]::IsNullOrWhiteSpace($expectedValue)) {
                continue
            }
            $currentValue = Get-OptionalText -Object $current -Name $field
            if ([string]::IsNullOrWhiteSpace($currentValue)) {
                $Failures.Add("$field missing for '$key'; expected $expectedValue.")
            } elseif (-not $currentValue.Equals($expectedValue, [System.StringComparison]::OrdinalIgnoreCase)) {
                $Failures.Add("$field mismatch for '$key': current $currentValue expected $expectedValue.")
            }
        }
    }
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
        $extension.Equals(".adz", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".ipf", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".hdf", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".hdz", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    if ($Path.EndsWith(".adf.gz", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    $isZip = $extension.Equals(".zip", [System.StringComparison]::OrdinalIgnoreCase)
    $isTarLike = $extension.Equals(".tar", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".tgz", [System.StringComparison]::OrdinalIgnoreCase) -or
        $Path.EndsWith(".tar.gz", [System.StringComparison]::OrdinalIgnoreCase)
    $isToolBacked = $extension.Equals(".7z", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".rar", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".lha", [System.StringComparison]::OrdinalIgnoreCase) -or
        $extension.Equals(".lzh", [System.StringComparison]::OrdinalIgnoreCase)
    if (-not $isZip -and -not $isTarLike -and -not $isToolBacked) {
        return $false
    }

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $entries = & tar -tf $Path 2>$null
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Could not inspect archive while scanning Amiga media: $Path"
            return $false
        }
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    foreach ($entry in $entries) {
        $entryExtension = [System.IO.Path]::GetExtension($entry)
        if ($entryExtension.Equals(".adf", [System.StringComparison]::OrdinalIgnoreCase) -or
            $entryExtension.Equals(".adz", [System.StringComparison]::OrdinalIgnoreCase) -or
            $entryExtension.Equals(".ipf", [System.StringComparison]::OrdinalIgnoreCase) -or
            $entryExtension.Equals(".hdf", [System.StringComparison]::OrdinalIgnoreCase) -or
            $entryExtension.Equals(".hdz", [System.StringComparison]::OrdinalIgnoreCase) -or
            $entry.EndsWith(".adf.gz", [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
        if (($isZip -or $isToolBacked) -and
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
    foreach ($extension in @(".adf", ".adz", ".gz", ".ipf", ".hdf", ".hdz", ".zip", ".tar", ".tgz", ".7z", ".rar", ".lha", ".lzh")) {
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
    param([Parameter(Mandatory = $true)][Alias("Path")][string]$Value)
    $stem = [System.IO.Path]::GetFileName($Value)
    if ([string]::IsNullOrWhiteSpace($stem)) {
        $stem = $Value
    }
    $stem = [System.Text.RegularExpressions.Regex]::Replace(
        $stem,
        "(?i)(\.adf\.gz|\.tar\.gz|\.adf|\.adz|\.ipf|\.hdf|\.hdz|\.zip|\.tar|\.tgz|\.7z|\.rar|\.lha|\.lzh)$",
        "")
    $safe = [System.Text.RegularExpressions.Regex]::Replace($stem, "[^A-Za-z0-9._-]+", "_")
    $safe = $safe.Trim("_")
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "media"
    }
    return $safe
}

function Test-MediaSelectorMatch {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Selector
    )
    $selectorText = $Selector.Trim()
    if ([string]::IsNullOrWhiteSpace($selectorText)) {
        return $false
    }

    $fileName = [System.IO.Path]::GetFileName($Path)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    return $Path.Equals($selectorText, [System.StringComparison]::OrdinalIgnoreCase) -or
        $fileName.Equals($selectorText, [System.StringComparison]::OrdinalIgnoreCase) -or
        $stem.Equals($selectorText, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-DiskSetMarker {
    param([Parameter(Mandatory = $true)][string]$Path)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    foreach ($pattern in @(
            '(?i)\bdisk[\s._-]*(\d+)\s*(?:of|/)\s*(\d+)\b',
            '(?i)(?<!\d)(\d+)\s*of\s*(\d+)(?!\d)')) {
        $match = [System.Text.RegularExpressions.Regex]::Match($stem, $pattern)
        if (-not $match.Success) {
            continue
        }
        $number = [int]$match.Groups[1].Value
        $total = [int]$match.Groups[2].Value
        if ($number -le 0 -or $total -le 1 -or $number -gt $total -or $total -gt 32) {
            continue
        }
        $groupKey = $stem.Remove($match.Index, $match.Length)
        $groupKey = [System.Text.RegularExpressions.Regex]::Replace($groupKey, '[\[\]\(\)]', ' ')
        $groupKey = [System.Text.RegularExpressions.Regex]::Replace($groupKey, '[\s._-]+', ' ')
        $groupKey = $groupKey.Trim()
        if ([string]::IsNullOrWhiteSpace($groupKey)) {
            continue
        }
        return [pscustomobject]@{
            Number = $number
            Total = $total
            GroupKey = $groupKey
        }
    }
    foreach ($pattern in @(
            '(?i)(?:^|[\s._-]+)disk[\s._-]*(\d+)\b',
            '(?i)[\s._-]+(\d+)$',
            '(?i)(?<=[A-Za-z])(\d+)$')) {
        $match = [System.Text.RegularExpressions.Regex]::Match($stem, $pattern)
        if (-not $match.Success) {
            continue
        }
        $number = [int]$match.Groups[1].Value
        if ($number -le 0 -or $number -gt 32) {
            continue
        }
        $groupKey = $stem.Remove($match.Index, $match.Length)
        $groupKey = [System.Text.RegularExpressions.Regex]::Replace($groupKey, '[\[\]\(\)]', ' ')
        $groupKey = [System.Text.RegularExpressions.Regex]::Replace($groupKey, '[\s._-]+', ' ')
        $groupKey = $groupKey.Trim()
        if ([string]::IsNullOrWhiteSpace($groupKey)) {
            continue
        }
        return [pscustomobject]@{
            Number = $number
            Total = 0
            GroupKey = $groupKey
        }
    }
    return $null
}

function New-MediaLaunchSets {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Paths
    )

    $groupMap = @{}
    for ($i = 0; $i -lt $Paths.Count; ++$i) {
        $marker = Get-DiskSetMarker -Path $Paths[$i]
        if ($null -eq $marker) {
            continue
        }
        $key = "$($marker.GroupKey.ToLowerInvariant())|$($marker.Total)"
        if (-not $groupMap.ContainsKey($key)) {
            $groupMap[$key] = [System.Collections.Generic.List[object]]::new()
        }
        $groupMap[$key].Add([pscustomobject]@{
                Path = $Paths[$i]
                Index = $i
                Number = $marker.Number
                Total = $marker.Total
                GroupKey = $marker.GroupKey
            })
    }

    $groupByPath = @{}
    $sets = [System.Collections.Generic.List[object]]::new()
    foreach ($key in $groupMap.Keys) {
        $members = $groupMap[$key]
        if ($members.Count -eq 0) {
            continue
        }
        $declaredTotal = [int]$members[0].Total
        $byNumber = @{}
        $duplicate = $false
        $highestNumber = 0
        foreach ($member in $members) {
            $numberKey = [string]$member.Number
            if ($byNumber.ContainsKey($numberKey)) {
                $duplicate = $true
                break
            }
            $byNumber[$numberKey] = $member
            $highestNumber = [Math]::Max($highestNumber, [int]$member.Number)
        }
        $total = if ($declaredTotal -gt 0) { $declaredTotal } else { $highestNumber }
        if ($duplicate -or $total -le 1 -or $total -gt 32 -or $byNumber.Count -ne $total) {
            continue
        }

        $groupPaths = [System.Collections.Generic.List[string]]::new()
        $firstIndex = [int]::MaxValue
        $complete = $true
        for ($disk = 1; $disk -le $total; ++$disk) {
            $numberKey = [string]$disk
            if (-not $byNumber.ContainsKey($numberKey)) {
                $complete = $false
                break
            }
            $member = $byNumber[$numberKey]
            [void]$groupPaths.Add($member.Path)
            $firstIndex = [Math]::Min($firstIndex, [int]$member.Index)
        }
        if (-not $complete) {
            continue
        }

        $mediaSet = [pscustomobject]@{
            Label = $members[0].GroupKey
            Paths = [string[]]$groupPaths.ToArray()
            SortIndex = $firstIndex
            Display = ($groupPaths.ToArray() -join ';')
        }
        foreach ($path in $groupPaths) {
            $groupByPath[$path] = $mediaSet
        }
    }

    $emittedGroups = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    for ($i = 0; $i -lt $Paths.Count; ++$i) {
        if ($groupByPath.ContainsKey($Paths[$i])) {
            $mediaSet = $groupByPath[$Paths[$i]]
            if ($emittedGroups.Add($mediaSet.Display)) {
                [void]$sets.Add($mediaSet)
            }
            continue
        }
        [void]$sets.Add([pscustomobject]@{
                Label = [System.IO.Path]::GetFileNameWithoutExtension($Paths[$i])
                Paths = [string[]]@($Paths[$i])
                SortIndex = $i
                Display = $Paths[$i]
            })
    }

    return [System.Collections.Generic.List[object]]::new(
        @($sets | Sort-Object SortIndex))
}

function Test-MediaLaunchSetSelectorMatch {
    param(
        [Parameter(Mandatory = $true)]$MediaSet,
        [Parameter(Mandatory = $true)][string]$Selector
    )
    $selectorText = $Selector.Trim()
    if ($MediaSet.Label.Equals($selectorText, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    foreach ($path in $MediaSet.Paths) {
        if (Test-MediaSelectorMatch -Path $path -Selector $selectorText) {
            return $true
        }
    }
    return $false
}

function Get-KickstartConfig {
    param([Parameter(Mandatory = $true)][string]$SystemName)
    switch ($SystemName) {
        "amiga1000" {
            return [pscustomobject]@{
                EnvVar = "MNEMOS_AMIGA1000_KICKSTART"
                Path = $Kickstart1000
            }
        }
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
        "amiga2000" {
            return [pscustomobject]@{
                EnvVar = "MNEMOS_AMIGA2000_KICKSTART"
                Path = $Kickstart2000
            }
        }
    }
}

function Get-KickstartCandidateNames {
    param([Parameter(Mandatory = $true)][string]$SystemName)
    switch ($SystemName) {
        "amiga1000" {
            return @(
                "Kickstart 1.0.rom",
                "Kickstart 1.0 (NTSC) (A1000) (Commodore) (1985).rom",
                "kick10.rom",
                "kickstart10.rom",
                "kickstart1.0.rom"
            )
        }
        "amiga500plus" {
            return @(
                "Kickstart 2.0.rom",
                "Kickstart 2.04.rom",
                "Kickstart 2.05.rom",
                "kick20.rom",
                "kick204.rom",
                "kick205.rom",
                "kickstart20.rom",
                "kickstart2.0.rom"
            )
        }
        "amiga600" {
            return @(
                "Kickstart 2.0.rom",
                "Kickstart 2.05.rom",
                "Kickstart 3.1.rom",
                "kick20.rom",
                "kick205.rom",
                "kick31.rom",
                "kickstart20.rom",
                "kickstart2.0.rom",
                "kickstart31.rom"
            )
        }
        default {
            return @(
                "Kickstart 1.3.rom",
                "Kickstart 1.2.rom",
                "kick13.rom",
                "kick12.rom",
                "kickstart13.rom",
                "kickstart12.rom",
                "kickstart1.3.rom",
                "kickstart1.2.rom"
            )
        }
    }
}

function Resolve-KickstartPath {
    param(
        [Parameter(Mandatory = $true)][string]$SystemName,
        [Parameter(Mandatory = $true)]$Kickstart
    )

    if (-not [string]::IsNullOrWhiteSpace($Kickstart.Path)) {
        return (Resolve-Path -LiteralPath $Kickstart.Path).Path
    }

    $dirs = @(
        [Environment]::GetEnvironmentVariable(($Kickstart.EnvVar -replace "_KICKSTART$", "_KICKSTART_DIR")),
        [Environment]::GetEnvironmentVariable(($Kickstart.EnvVar -replace "_KICKSTART$", "_BIOS_DIR")),
        [Environment]::GetEnvironmentVariable("MNEMOS_AMIGA_KICKSTART_DIR"),
        [Environment]::GetEnvironmentVariable("MNEMOS_AMIGA_BIOS_DIR")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach ($dir in $dirs) {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
            continue
        }
        foreach ($name in (Get-KickstartCandidateNames -SystemName $SystemName)) {
            $candidate = Join-Path $dir $name
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    return ""
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
    $exitCode = 1
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    if ($hasNativeErrorPreference) {
        $previousNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
    }
    try {
        $ErrorActionPreference = "Continue"
        & $Player @Arguments *> $LogPath
        $exitCode = $LASTEXITCODE
    } finally {
        $stopwatch.Stop()
        $ErrorActionPreference = $previousErrorActionPreference
        if ($hasNativeErrorPreference) {
            $PSNativeCommandUseErrorActionPreference = $previousNativeErrorPreference
        }
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        ElapsedSeconds = $stopwatch.Elapsed.TotalSeconds
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
    $colorCounts = @{}
    $dominantColor = 0
    $dominantColorPixels = 0
    $topBandRows = [Math]::Min(48, $height)
    $topBandPixels = $width * $topBandRows
    $topBandRed = 0
    $pixelIndex = 0
    for ($i = $offset.Value; $i -lt $needed; $i += 3) {
        $r = $bytes[$i]
        $g = $bytes[$i + 1]
        $b = $bytes[$i + 2]
        if ($r -ne 0 -or $g -ne 0 -or $b -ne 0) {
            ++$nonBlack
        }
        if ($pixelIndex -lt $topBandPixels -and $r -ge 180 -and $g -le 80 -and $b -le 80) {
            ++$topBandRed
        }
        $color = ($r -shl 16) -bor ($g -shl 8) -bor $b
        if ($colorCounts.ContainsKey($color)) {
            $colorCounts[$color] = [int]$colorCounts[$color] + 1
        } else {
            $colorCounts[$color] = 1
        }
        if ([int]$colorCounts[$color] -gt $dominantColorPixels) {
            $dominantColor = $color
            $dominantColorPixels = [int]$colorCounts[$color]
        }
        ++$pixelIndex
    }

    return [pscustomobject]@{
        Width = $width
        Height = $height
        Pixels = $pixels
        NonBlackPixels = $nonBlack
        UniqueColors = $colorCounts.Count
        DominantColor = ("#{0:X6}" -f $dominantColor)
        DominantColorPixels = $dominantColorPixels
        DominantColorRatio = if ($pixels -gt 0) { [double]$dominantColorPixels / [double]$pixels } else { 0.0 }
        TopBandRedPixels = $topBandRed
        TopBandRedRatio = if ($topBandPixels -gt 0) { [double]$topBandRed / [double]$topBandPixels } else { 0.0 }
        Sha256 = Get-FileSha256 -Path $Path
    }
}

function Get-AmigaDisplayClassification {
    param([Parameter(Mandatory = $true)]$Stats)
    if ($Stats.Width -eq 320 -and $Stats.Height -eq 256 -and
        $Stats.NonBlackPixels -eq 79293) {
        return "kickstart_1_3_insert_disk_prompt"
    }
    switch ($Stats.Sha256) {
        "4F228C20463809C5947548EACFECDDDE5EA5C85891EED25C3AE6192A234C27DF" {
            return "kickstart_2_0_insert_disk_prompt"
        }
        "726C30DEA667C0DABF2493219C57BAB161850E7BD986F8F832DEC0EA5F6C74E2" {
            return "kickstart_2_0_insert_disk_prompt"
        }
        "B7ACC6ECA5FE662FE9542047B3AC9C0DD9C7CC49A43438CD34BFD92CD461B03F" {
            return "kickstart_2_05_insert_disk_prompt"
        }
        "F92ACD05C21EF970120F7D66B06E876188D82D5D046DBD9EB8EAF65330F93E22" {
            return "kickstart_3_1_or_4_0_insert_disk_prompt"
        }
    }
    if ($Stats.TopBandRedRatio -ge 0.04 -and
        $Stats.UniqueColors -le 8 -and
        $Stats.DominantColorRatio -ge 0.70) {
        return "amiga_software_failure"
    }
    return "unknown_or_booted"
}

function Get-RenderedAudioStats {
    param([Parameter(Mandatory = $true)][string]$BasePath)
    $tracePath = "$BasePath.rendered_audio.json"
    $wavPath = "$BasePath.rendered.wav"
    if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) {
        throw "Rendered audio trace not written: $tracePath"
    }
    if (-not (Test-Path -LiteralPath $wavPath -PathType Leaf)) {
        throw "Rendered audio WAV not written: $wavPath"
    }

    $trace = Get-Content -Raw -LiteralPath $tracePath | ConvertFrom-Json
    $framesWithSignal = 0
    $peakAbs = 0
    foreach ($frame in $trace.frame_metrics) {
        if ($frame.nonzero_frames -gt 0) {
            ++$framesWithSignal
        }
        if ($frame.peak_abs -gt $peakAbs) {
            $peakAbs = [int]$frame.peak_abs
        }
    }

    return [pscustomobject]@{
        CapturedFrames = [int64]$trace.captured_frames
        SampleRate = [int]$trace.sample_rate
        FramesWithSignal = $framesWithSignal
        PeakAbs = $peakAbs
        Wav = $wavPath
        WavSha256 = Get-FileSha256 -Path $wavPath
        Trace = $tracePath
        TraceSha256 = Get-FileSha256 -Path $tracePath
    }
}

function Get-RegisterDumpValues {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Register dump not written: $Path"
    }

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line.StartsWith("#")) {
            continue
        }
        if ($line -match '^([^ ]+) .* value=0x([0-9A-Fa-f]+)$') {
            $values[$Matches[1]] = [Convert]::ToInt64($Matches[2], 16)
        }
    }
    return $values
}

function Get-AmigaBoardStats {
    param([Parameter(Mandatory = $true)][string]$ScreenshotPath)
    $registerPath = "$ScreenshotPath.amiga_board.regs.txt"
    $values = Get-RegisterDumpValues -Path $registerPath
    foreach ($name in @("DSKPTR", "DSKDMA", "DFMOTOR", "DFCyl")) {
        if (-not $values.ContainsKey($name)) {
            throw "Amiga board register '$name' is missing from $registerPath"
        }
    }

    return [pscustomobject]@{
        DiskPointer = [int64]$values["DSKPTR"]
        DiskDmaBytesRemaining = [int64]$values["DSKDMA"]
        DriveMotor = [int]$values["DFMOTOR"]
        DriveCylinder = [int]$values["DFCyl"]
        RegisterDump = $registerPath
    }
}

function Get-ContentProbeFrameList {
    param(
        [Parameter(Mandatory = $true)][int]$DefaultFrames,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$ProbeFrames
    )

    $frames = [System.Collections.Generic.HashSet[int]]::new()
    [void]$frames.Add($DefaultFrames)
    foreach ($value in $ProbeFrames) {
        foreach ($token in $value.Split(",", [System.StringSplitOptions]::RemoveEmptyEntries)) {
            $trimmed = $token.Trim()
            if ([string]::IsNullOrWhiteSpace($trimmed)) {
                continue
            }
            $frame = 0
            if (-not [int]::TryParse(
                    $trimmed,
                    [System.Globalization.NumberStyles]::Integer,
                    [System.Globalization.CultureInfo]::InvariantCulture,
                    [ref]$frame)) {
                throw "-ContentProbeFrames contains a non-integer value: $trimmed"
            }
            if ($frame -le 0) {
                throw "-ContentProbeFrames values must be positive."
            }
            [void]$frames.Add($frame)
        }
    }
    return @($frames | Sort-Object)
}

function Get-FlatArgumentList {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$Values
    )

    $flat = [System.Collections.Generic.List[string]]::new()
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($token in $value.Split(",", [System.StringSplitOptions]::RemoveEmptyEntries)) {
            $trimmed = $token.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
                $flat.Add($trimmed)
            }
        }
    }
    return $flat.ToArray()
}

function Get-AmigaSystemList {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$Values
    )

    $validSystems = @("amiga1000", "amiga500", "amiga500plus", "amiga600", "amiga2000")
    $validLookup = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]$validSystems,
        [System.StringComparer]::OrdinalIgnoreCase)
    $systems = [System.Collections.Generic.List[string]]::new()
    foreach ($token in (Get-FlatArgumentList -Values $Values)) {
        $systemName = $token.ToLowerInvariant()
        if (-not $validLookup.Contains($systemName)) {
            throw "-System contains unsupported Amiga model '$token'. Valid values: $($validSystems -join ', ')."
        }
        $systems.Add($systemName)
    }
    if ($systems.Count -eq 0) {
        throw "-System must include at least one Amiga model."
    }
    return $systems.ToArray()
}

function Get-AmigaContentProbeScore {
    param(
        [Parameter(Mandatory = $true)]$Stats,
        [Parameter(Mandatory = $true)][string]$DisplayClassification,
        $BoardStats
    )

    [int64]$score = 0
    if ($DisplayClassification -notlike "kickstart_*_insert_disk_prompt" -and
        $DisplayClassification -ne "amiga_software_failure") {
        $score += 1000000000
    }
    if ($Stats.NonBlackPixels -gt 0) {
        $score += 100000000
    }
    if ($Stats.UniqueColors -gt 1) {
        $score += 10000000
    }
    if ($null -ne $BoardStats) {
        if ($BoardStats.DiskPointer -ne 0 -or
            $BoardStats.DiskDmaBytesRemaining -ne 0 -or
            $BoardStats.DriveCylinder -ne 0) {
            $score += 1000000
        }
        $score += [int64]([Math]::Min(999, [int]$BoardStats.DriveCylinder) * 1000)
    }

    $score += [int64][Math]::Min(999999, $Stats.UniqueColors * 1000)
    $score += [int64][Math]::Min(999999, [Math]::Floor($Stats.NonBlackPixels / 10.0))
    $score += [int64][Math]::Floor((1.0 - [Math]::Min(1.0, $Stats.DominantColorRatio)) * 100000.0)
    return $score
}

if ($Frames -le 0) {
    throw "-Frames must be positive."
}
$contentProbeFrameList = Get-ContentProbeFrameList -DefaultFrames $Frames -ProbeFrames $ContentProbeFrames
if ($MaxSets -lt 0) {
    throw "-MaxSets cannot be negative."
}
if (-not [string]::IsNullOrWhiteSpace($StartAfter) -and $MaxSets -eq 0) {
    Write-Warning "-StartAfter is usually paired with -MaxSets; continuing with every later media entry."
}
if ($MinimumHeadlessFps -lt 0.0) {
    throw "-MinimumHeadlessFps cannot be negative."
}
if ($MinimumUniqueColors -lt 0) {
    throw "-MinimumUniqueColors cannot be negative."
}
if ($MaximumDominantColorRatio -lt 0.0 -or $MaximumDominantColorRatio -gt 1.0) {
    throw "-MaximumDominantColorRatio must be between 0.0 and 1.0."
}
if ($MinimumDiskCylinder -lt -1) {
    throw "-MinimumDiskCylinder cannot be below -1."
}
if ($AudioFrames -lt 0) {
    throw "-AudioFrames cannot be negative."
}
if ($MinimumAudioFramesWithSignal -lt 0) {
    throw "-MinimumAudioFramesWithSignal cannot be negative."
}
if ($MinimumAudioPeakAbs -lt 0) {
    throw "-MinimumAudioPeakAbs cannot be negative."
}

$pressArgs = @(Get-FlatArgumentList -Values $Press)
$audioPressArgs = @(Get-FlatArgumentList -Values $AudioPress)
$systemList = @(Get-AmigaSystemList -Values $System)

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
$launchSets = [System.Collections.Generic.List[object]]::new(@(New-MediaLaunchSets -Paths $deduped))
if (-not [string]::IsNullOrWhiteSpace($StartAfter)) {
    $startIndex = -1
    for ($i = 0; $i -lt $launchSets.Count; ++$i) {
        if (Test-MediaLaunchSetSelectorMatch -MediaSet $launchSets[$i] -Selector $StartAfter) {
            $startIndex = $i
            break
        }
    }
    if ($startIndex -lt 0) {
        throw "-StartAfter did not match any discovered Amiga media: $StartAfter"
    }
    $remaining = $launchSets.Count - ($startIndex + 1)
    if ($remaining -le 0) {
        $launchSets = [System.Collections.Generic.List[object]]::new()
    } else {
        $launchSets = [System.Collections.Generic.List[object]]::new(
            $launchSets.GetRange($startIndex + 1, $remaining))
    }
}
if ($MaxSets -gt 0 -and $launchSets.Count -gt $MaxSets) {
    $launchSets = [System.Collections.Generic.List[object]]::new($launchSets.GetRange(0, $MaxSets))
}
if ($launchSets.Count -eq 0) {
    throw "No Amiga media found. Pass -Rom/-RomDir or set MNEMOS_AMIGA_ROM, MNEMOS_AMIGA_ADF_DIR, or MNEMOS_AMIGA_SET_DIR."
}
if ($ListSets) {
    $launchSets |
        ForEach-Object {
            [pscustomobject]@{
                MediaLabel = $_.Label
                MediaCount = $_.Paths.Count
                Media = $_.Display
            }
        } |
        Format-Table -AutoSize
    return
}

$buildPath = Resolve-RepoPath $BuildDir
$player = Join-Path $buildPath "src/apps/player/mnemos_player.exe"
if (-not (Test-Path -LiteralPath $player -PathType Leaf)) {
    throw "Player executable not found at '$player'. Build mnemos_player first, or pass -BuildDir."
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
    foreach ($systemName in $systemList) {
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
        $effectiveKickstartPath = Resolve-KickstartPath -SystemName $systemName -Kickstart $kickstart
        $effectiveKickstartLabel = if ([string]::IsNullOrWhiteSpace($effectiveKickstartPath)) {
            ""
        } else {
            [System.IO.Path]::GetFileNameWithoutExtension($effectiveKickstartPath)
        }
        try {
            foreach ($mediaSet in $launchSets) {
                $paths = [string[]]$mediaSet.Paths
                $path = $paths[0]
                $mediaLabel = [string]$mediaSet.Label
                $mediaDisplay = [string]$mediaSet.Display
                $safeName = "{0}-{1}" -f $systemName, (Get-SafeName -Path $mediaLabel)
                $useContentProbe = $ContentProbeFrames.Count -gt 0
                $probeResults = [System.Collections.Generic.List[object]]::new()
                foreach ($probeFrames in $contentProbeFrameList) {
                    $probeSuffix = if ($useContentProbe) {
                        ".f$probeFrames"
                    } else {
                        ""
                    }
                    $screenshot = Join-Path $artifactDir "$safeName$probeSuffix.ppm"
                    $log = Join-Path $artifactDir "$safeName$probeSuffix.log"
                    $args = @(
                        "--system", $systemName,
                        "--rom", $path,
                        "--frames", $probeFrames.ToString([System.Globalization.CultureInfo]::InvariantCulture),
                        "--screenshot", $screenshot
                    )
                    if (-not [string]::IsNullOrWhiteSpace($effectiveKickstartPath)) {
                        $args += @("--amiga-kickstart", $effectiveKickstartPath)
                    }
                    for ($mediaIndex = 1; $mediaIndex -lt $paths.Count; ++$mediaIndex) {
                        $args += @("--disk", $paths[$mediaIndex])
                    }
                    foreach ($press in $pressArgs) {
                        $args += @("--press", $press)
                    }

                    $run = Invoke-Player -Player $player -LogPath $log -Arguments $args
                    $headlessFps = 0.0
                    if ($run.ElapsedSeconds -gt 0.0) {
                        $headlessFps = [double]$probeFrames / [double]$run.ElapsedSeconds
                    }
                    $stats = $null
                    $displayClassification = ""
                    $boardStats = $null
                    $validationError = ""
                    $score = [int64]::MinValue
                    if ($run.ExitCode -eq 0) {
                        try {
                            $stats = Get-PpmStats -Path $screenshot
                            $displayClassification = Get-AmigaDisplayClassification -Stats $stats
                            if ($RequireDiskProgress) {
                                $boardStats = Get-AmigaBoardStats -ScreenshotPath $screenshot
                            }
                            $score = Get-AmigaContentProbeScore `
                                -Stats $stats `
                                -DisplayClassification $displayClassification `
                                -BoardStats $boardStats
                        } catch {
                            $validationError = $_.Exception.Message
                        }
                    }

                    $probeResults.Add([pscustomobject]@{
                        Frames = $probeFrames
                        Screenshot = $screenshot
                        Log = $log
                        Run = $run
                        HeadlessFps = $headlessFps
                        Stats = $stats
                        DisplayClassification = $displayClassification
                        BoardStats = $boardStats
                        ValidationError = $validationError
                        Score = $score
                    })
                }

                if ($RejectSoftwareFailure) {
                    $softwareFailureProbe = @($probeResults |
                        Where-Object { $_.Run.ExitCode -eq 0 -and $_.DisplayClassification -eq "amiga_software_failure" } |
                        Sort-Object -Property Frames -Descending |
                        Select-Object -First 1)
                    if ($softwareFailureProbe.Count -gt 0) {
                        $probe = $softwareFailureProbe[0]
                        $failures.Add("$systemName reached Amiga software failure for '$mediaDisplay' at frame $($probe.Frames). See $($probe.Screenshot)")
                        continue
                    }
                }

                $selectedProbe = if ($useContentProbe) {
                    @($probeResults |
                        Where-Object { $_.Run.ExitCode -eq 0 -and $null -ne $_.Stats -and [string]::IsNullOrWhiteSpace($_.ValidationError) } |
                        Sort-Object -Property Score, Frames -Descending |
                        Select-Object -First 1)
                } else {
                    $probeResults[0]
                }
                if ($null -eq $selectedProbe -or @($selectedProbe).Count -eq 0) {
                    $probeErrors = @($probeResults | ForEach-Object {
                            if ($_.Run.ExitCode -ne 0) {
                                "frame $($_.Frames): exit code $($_.Run.ExitCode), see $($_.Log)"
                            } elseif (-not [string]::IsNullOrWhiteSpace($_.ValidationError)) {
                                "frame $($_.Frames): $($_.ValidationError), see $($_.Screenshot)"
                            } else {
                                "frame $($_.Frames): no usable screenshot, see $($_.Screenshot)"
                            }
                        })
                    $failures.Add("$systemName found no usable content probe for '$mediaDisplay'. $($probeErrors -join '; ')")
                    continue
                }
                if ($selectedProbe -is [array]) {
                    $selectedProbe = $selectedProbe[0]
                }

                $run = $selectedProbe.Run
                $headlessFps = [double]$selectedProbe.HeadlessFps
                $screenshot = [string]$selectedProbe.Screenshot
                $log = [string]$selectedProbe.Log
                $selectedFrames = [int]$selectedProbe.Frames
                if ($run.ExitCode -ne 0) {
                    $failures.Add("$systemName failed for '$mediaDisplay' with exit code $($run.ExitCode). See $log")
                    continue
                }
                if (-not [string]::IsNullOrWhiteSpace($selectedProbe.ValidationError)) {
                    $failures.Add("$systemName could not validate screenshot for '$mediaDisplay': $($selectedProbe.ValidationError)")
                    continue
                }
                if ($MinimumHeadlessFps -gt 0.0 -and $headlessFps -lt $MinimumHeadlessFps) {
                    $fpsText = $headlessFps.ToString("F2", [System.Globalization.CultureInfo]::InvariantCulture)
                    $minimumText = $MinimumHeadlessFps.ToString("F2", [System.Globalization.CultureInfo]::InvariantCulture)
                    $failures.Add("$systemName ran '$mediaDisplay' at $fpsText FPS, below -MinimumHeadlessFps $minimumText. See $log")
                }

                try {
                    $stats = $selectedProbe.Stats
                    if (-not $AllowBlackFrame -and $stats.NonBlackPixels -eq 0) {
                        $failures.Add("$systemName produced an all-black frame for '$mediaDisplay'. See $screenshot")
                    }
                    if ($RejectFlatFrame -and $stats.UniqueColors -le 1) {
                        $failures.Add("$systemName produced a flat $($stats.DominantColor) frame for '$mediaDisplay'. See $screenshot")
                    }
                    if ($MinimumUniqueColors -gt 0 -and $stats.UniqueColors -lt $MinimumUniqueColors) {
                        $failures.Add("$systemName produced $($stats.UniqueColors) unique color(s) for '$mediaDisplay', below -MinimumUniqueColors $MinimumUniqueColors. See $screenshot")
                    }
                    if ($MaximumDominantColorRatio -gt 0.0 -and
                        $stats.DominantColorRatio -gt $MaximumDominantColorRatio) {
                        $dominantText = $stats.DominantColorRatio.ToString("P2", [System.Globalization.CultureInfo]::InvariantCulture)
                        $maximumText = $MaximumDominantColorRatio.ToString("P2", [System.Globalization.CultureInfo]::InvariantCulture)
                        $failures.Add("$systemName dominant color $($stats.DominantColor) covers $dominantText of '$mediaDisplay', above -MaximumDominantColorRatio $maximumText. See $screenshot")
                    }
                    $displayClassification = [string]$selectedProbe.DisplayClassification
                    if ($RejectKickstartPrompt -and
                        $displayClassification -like "kickstart_*_insert_disk_prompt") {
                        $failures.Add("$systemName stopped at $displayClassification for '$mediaDisplay'. See $screenshot")
                    }
                    $boardStats = $selectedProbe.BoardStats
                    if ($RequireDiskProgress) {
                        try {
                            if ($null -eq $boardStats) {
                                $boardStats = Get-AmigaBoardStats -ScreenshotPath $screenshot
                            }
                            if ($boardStats.DiskPointer -eq 0 -and
                                $boardStats.DiskDmaBytesRemaining -eq 0 -and
                                $boardStats.DriveCylinder -eq 0) {
                                $failures.Add("$systemName showed no Amiga disk progress for '$mediaDisplay' (zero disk DMA pointer, zero DMA remainder, and cylinder 0). See $($boardStats.RegisterDump)")
                            }
                            if ($MinimumDiskCylinder -ge 0 -and
                                $boardStats.DriveCylinder -lt $MinimumDiskCylinder) {
                                $failures.Add("$systemName reached floppy cylinder $($boardStats.DriveCylinder) for '$mediaDisplay', below -MinimumDiskCylinder $MinimumDiskCylinder. See $($boardStats.RegisterDump)")
                            }
                        } catch {
                            $failures.Add("$systemName could not validate Amiga disk progress for '$mediaDisplay': $($_.Exception.Message)")
                        }
                    }
                    $audioStats = $null
                    if ($RequireRenderedAudio) {
                        $audioFrameCount = if ($AudioFrames -gt 0) { $AudioFrames } else { $selectedFrames }
                        $audioBase = Join-Path $artifactDir "$safeName.audio"
                        $audioLog = Join-Path $artifactDir "$safeName.audio.log"
                        $audioArgs = @(
                            "--system", $systemName,
                            "--rom", $path,
                            "--extract-audio", $audioBase,
                            "--extract-frames", $audioFrameCount.ToString([System.Globalization.CultureInfo]::InvariantCulture)
                        )
                        if (-not [string]::IsNullOrWhiteSpace($effectiveKickstartPath)) {
                            $audioArgs += @("--amiga-kickstart", $effectiveKickstartPath)
                        }
                        for ($mediaIndex = 1; $mediaIndex -lt $paths.Count; ++$mediaIndex) {
                            $audioArgs += @("--disk", $paths[$mediaIndex])
                        }
                        foreach ($press in $audioPressArgs) {
                            $audioArgs += @("--press", $press)
                        }

                        $audioRun = Invoke-Player -Player $player -LogPath $audioLog -Arguments $audioArgs
                        if ($audioRun.ExitCode -ne 0) {
                            $failures.Add("$systemName audio export failed for '$mediaDisplay' with exit code $($audioRun.ExitCode). See $audioLog")
                        } else {
                            try {
                                $audioStats = Get-RenderedAudioStats -BasePath $audioBase
                                if ($audioStats.FramesWithSignal -lt $MinimumAudioFramesWithSignal) {
                                    $failures.Add("$systemName rendered audio for '$mediaDisplay' has $($audioStats.FramesWithSignal) signal frame(s), below -MinimumAudioFramesWithSignal $MinimumAudioFramesWithSignal. See $($audioStats.Trace)")
                                }
                                if ($audioStats.PeakAbs -lt $MinimumAudioPeakAbs) {
                                    $failures.Add("$systemName rendered audio for '$mediaDisplay' peak $($audioStats.PeakAbs), below -MinimumAudioPeakAbs $MinimumAudioPeakAbs. See $($audioStats.Trace)")
                                }
                            } catch {
                                $failures.Add("$systemName could not validate rendered audio for '$mediaDisplay': $($_.Exception.Message)")
                            }
                        }
                    }

                    $results.Add([pscustomobject]@{
                        System = $systemName
                        Media = $mediaDisplay
                        MediaLabel = $mediaLabel
                        MediaCount = $paths.Count
                        Kickstart = $effectiveKickstartLabel
                        KickstartPath = $effectiveKickstartPath
                        Frames = $selectedFrames
                        RequestedFrames = $Frames
                        ContentProbeFrames = if ($useContentProbe) { ($contentProbeFrameList -join ",") } else { "" }
                        ContentProbeScore = if ($useContentProbe) { $selectedProbe.Score } else { $null }
                        Press = if ($pressArgs.Count -gt 0) { ($pressArgs -join ",") } else { "" }
                        AudioPress = if ($audioPressArgs.Count -gt 0) { ($audioPressArgs -join ",") } else { "" }
                        ElapsedSeconds = [Math]::Round($run.ElapsedSeconds, 3)
                        HeadlessFps = [Math]::Round($headlessFps, 2)
                        Width = $stats.Width
                        Height = $stats.Height
                        NonBlackPixels = $stats.NonBlackPixels
                        UniqueColors = $stats.UniqueColors
                        DominantColor = $stats.DominantColor
                        DominantColorPixels = $stats.DominantColorPixels
                        DominantColorRatio = [Math]::Round($stats.DominantColorRatio, 4)
                        TopBandRedPixels = $stats.TopBandRedPixels
                        TopBandRedRatio = [Math]::Round($stats.TopBandRedRatio, 4)
                        ScreenshotSha256 = $stats.Sha256
                        DisplayClassification = $displayClassification
                        DiskPointer = if ($null -ne $boardStats) { $boardStats.DiskPointer } else { $null }
                        DiskDmaBytesRemaining = if ($null -ne $boardStats) { $boardStats.DiskDmaBytesRemaining } else { $null }
                        DriveMotor = if ($null -ne $boardStats) { $boardStats.DriveMotor } else { $null }
                        DriveCylinder = if ($null -ne $boardStats) { $boardStats.DriveCylinder } else { $null }
                        AudioCapturedFrames = if ($null -ne $audioStats) { $audioStats.CapturedFrames } else { $null }
                        AudioFramesWithSignal = if ($null -ne $audioStats) { $audioStats.FramesWithSignal } else { $null }
                        AudioPeakAbs = if ($null -ne $audioStats) { $audioStats.PeakAbs } else { $null }
                        Screenshot = $screenshot
                        AudioWav = if ($null -ne $audioStats) { $audioStats.Wav } else { $null }
                        AudioWavSha256 = if ($null -ne $audioStats) { $audioStats.WavSha256 } else { $null }
                        AudioTrace = if ($null -ne $audioStats) { $audioStats.Trace } else { $null }
                        AudioTraceSha256 = if ($null -ne $audioStats) { $audioStats.TraceSha256 } else { $null }
                        Log = $log
                    })
                } catch {
                    $failures.Add("$systemName could not validate screenshot for '$mediaDisplay': $($_.Exception.Message)")
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

$summaryCsv = Join-Path $artifactDir "summary.csv"
$summaryJson = Join-Path $artifactDir "summary.json"
$results | Export-Csv -NoTypeInformation -Path $summaryCsv
$results | ConvertTo-Json -Depth 3 | Set-Content -Encoding utf8NoBOM -Path $summaryJson

if (-not [string]::IsNullOrWhiteSpace($ExpectedSummary)) {
    Add-ExpectedSummaryFailures -Results $results `
        -Path $ExpectedSummary `
        -RequireRows $RequireExpectedRows.IsPresent `
        -Failures $failures
}

$results | Format-Table -AutoSize System, Frames, HeadlessFps, Width, Height, NonBlackPixels, UniqueColors, DominantColorRatio, DriveCylinder, AudioFramesWithSignal, AudioPeakAbs, Media

if ($failures.Count -gt 0) {
    throw "Amiga corpus smoke failed:`n$($failures -join "`n")"
}

Write-Host "Amiga corpus smoke passed for $($results.Count) launch(es). Artifacts: $artifactDir"
Write-Host "Summary: $summaryCsv"
