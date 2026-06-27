#!/usr/bin/env pwsh
# Data-gated Irem M72 corpus smoke runner.
#
# ROM sets are never committed. Point this at one or more zips/directories with -Rom,
# MNEMOS_M72_RTYPE_SET, MNEMOS_M72_PROTECTED_SET,
# MNEMOS_M72_PROTECTED_MCU_SET, MNEMOS_M72_VERTICAL_SET, or at true-M72
# roster directories with -RomDir / MNEMOS_M72_SET_DIR. Pass
# -Recurse for mixed corpus roots such as D:\emu\irem; the top-level
# scripts/run-data-gated-tests.ps1 entrypoint does this for M72 automatically.
# Use -Set to narrow a mixed root to one or more checked-in M72 manifest ids.
# Use -RequireRenderedAudio to require an --extract-audio pass whose rendered
# WAVE payload contains nonzero PCM. -AudioFrames controls that proof pass;
# known late-audio sets may raise the effective proof window.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string[]]$Rom = @(),
    [string[]]$RomDir = @(),
    [string[]]$Set = @(),
    [int]$Frames = 600,
    [string[]]$FallbackFrames = @("300", "900"),
    [int]$MaxSets = 0,
    [switch]$IncludeAllZips,
    [switch]$Recurse,
    [switch]$RequireRenderedAudio,
    [int]$AudioFrames = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression

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

function Split-CommaList {
    param([AllowEmptyCollection()][string[]]$Values)
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($part in $value.Split(',', [System.StringSplitOptions]::RemoveEmptyEntries)) {
            $trimmed = $part.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
                $trimmed
            }
        }
    }
}

function ConvertTo-PositiveIntList {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Values,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $result = [System.Collections.Generic.List[int]]::new()
    foreach ($part in Split-CommaList $Values) {
        $parsed = 0
        if (-not [int]::TryParse($part, [ref]$parsed)) {
            throw "$Name value '$part' is not an integer."
        }
        if ($parsed -le 0) {
            throw "$Name values must be positive."
        }
        $result.Add($parsed)
    }
    return @($result)
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
    if ((Test-Path -LiteralPath $resolved -PathType Leaf) -or
        (Test-Path -LiteralPath $resolved -PathType Container)) {
        $Paths.Add((Resolve-Path -LiteralPath $resolved).Path)
    } else {
        Write-Warning "Irem M72 ROM path not found: $Path"
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

function Get-M72MediaValidationIssues {
    param([Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$LogPaths)
    $issues = [System.Collections.Generic.List[string]]::new()
    foreach ($logPath in $LogPaths) {
        if ([string]::IsNullOrWhiteSpace($logPath) -or
            -not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
            continue
        }
        foreach ($line in Get-Content -LiteralPath $logPath) {
            if ($line.StartsWith("[mnemos_player] media validation issue:", [System.StringComparison]::Ordinal)) {
                if (-not $issues.Contains($line)) {
                    $issues.Add($line)
                }
            }
        }
    }
    return @($issues)
}

function Get-M72ResolvedSetFromLog {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$LogPaths,
        [Parameter(Mandatory = $true)][string]$DefaultSet
    )
    foreach ($logPath in $LogPaths) {
        if ([string]::IsNullOrWhiteSpace($logPath) -or
            -not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
            continue
        }
        foreach ($line in Get-Content -LiteralPath $logPath) {
            if ($line -match "^\[irem_m72\] ROM source id '([^']+)' matched embedded set '([^']+)' by (maincpu CRCs|canonical M72 suffix)$") {
                return [pscustomobject]@{
                    Set = $Matches[2]
                    InferredFrom = $Matches[1]
                }
            }
        }
    }
    return [pscustomobject]@{
        Set = $DefaultSet
        InferredFrom = $null
    }
}

function Get-M72ManifestSetIds {
    $gamesDir = Join-Path $repoRoot "src/manifests/irem_m72/games"
    $ids = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    if (Test-Path -LiteralPath $gamesDir -PathType Container) {
        foreach ($toml in Get-ChildItem -LiteralPath $gamesDir -Filter "*.toml" -File) {
            [void]$ids.Add([System.IO.Path]::GetFileNameWithoutExtension($toml.Name))
        }
    }
    return $ids
}

function Get-M72ManifestParents {
    $gamesDir = Join-Path $repoRoot "src/manifests/irem_m72/games"
    $parents = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    if (Test-Path -LiteralPath $gamesDir -PathType Container) {
        foreach ($toml in Get-ChildItem -LiteralPath $gamesDir -Filter "*.toml" -File) {
            foreach ($line in Get-Content -LiteralPath $toml.FullName) {
                if ($line -match '^\s*parent\s*=\s*"([^"]+)"') {
                    $parents[[System.IO.Path]::GetFileNameWithoutExtension($toml.Name)] = $Matches[1]
                    break
                }
            }
        }
    }
    return $parents
}

function Get-M72NestedZipSetId {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$ManifestIds
    )
    if ([System.IO.Path]::GetExtension($Path) -ine ".zip") {
        return $null
    }
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            $archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Read, $false)
            try {
                $files = @($archive.Entries | Where-Object {
                    -not [string]::IsNullOrEmpty($_.Name) -and
                    -not $_.FullName.EndsWith("/", [System.StringComparison]::Ordinal)
                })
                if ($files.Count -ne 1) {
                    return $null
                }
                $entry = $files[0]
                if (-not $entry.FullName.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
                    return $null
                }
                $stem = [System.IO.Path]::GetFileNameWithoutExtension($entry.FullName)
                if ($ManifestIds.Contains($stem)) {
                    return $stem
                }
            } finally {
                $archive.Dispose()
            }
        } finally {
            $stream.Dispose()
        }
    } catch {
        return $null
    }
    return $null
}

function Get-M72CollectionZipSetIds {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$ManifestIds
    )
    $ids = [System.Collections.Generic.List[string]]::new()
    if ([System.IO.Path]::GetExtension($Path) -ine ".zip") {
        return @()
    }
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            $archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Read, $false)
            try {
                foreach ($entry in $archive.Entries) {
                    if ([string]::IsNullOrEmpty($entry.Name)) {
                        continue
                    }
                    $normalized = $entry.FullName.Replace('\', '/')
                    $slash = $normalized.IndexOf('/')
                    if ($slash -le 0) {
                        continue
                    }
                    $top = $normalized.Substring(0, $slash)
                    if ($ManifestIds.Contains($top) -and -not $ids.Contains($top)) {
                        $ids.Add($top)
                    }
                    $m72SuffixedTop = $top + "m72"
                    if ($ManifestIds.Contains($m72SuffixedTop) -and -not $ids.Contains($m72SuffixedTop)) {
                        $ids.Add($m72SuffixedTop)
                    }
                }
            } finally {
                $archive.Dispose()
            }
        } finally {
            $stream.Dispose()
        }
    } catch {
        return @()
    }
    return @($ids)
}

function New-M72CollectionSubsetZip {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$SetId,
        [Parameter(Mandatory = $true)][string]$OutDir
    )
    if ([System.IO.Path]::GetExtension($Path) -ine ".zip") {
        return $null
    }
    $sourceStem = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $safeStem = [System.Text.RegularExpressions.Regex]::Replace($sourceStem, '[^A-Za-z0-9_.-]', '_')
    $sourceDir = Join-Path (Join-Path $OutDir "_sources") $safeStem
    New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null
    $subsetPath = Join-Path $sourceDir ("{0}.zip" -f $SetId)
    if (Test-Path -LiteralPath $subsetPath -PathType Leaf) {
        Remove-Item -LiteralPath $subsetPath -Force
    }

    $copied = 0
    try {
        $inputStream = [System.IO.File]::OpenRead($Path)
        try {
            $inputArchive = [System.IO.Compression.ZipArchive]::new($inputStream, [System.IO.Compression.ZipArchiveMode]::Read, $false)
            try {
                $outputStream = [System.IO.File]::Open($subsetPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::ReadWrite)
                try {
                    $outputArchive = [System.IO.Compression.ZipArchive]::new($outputStream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
                    try {
                        foreach ($entry in $inputArchive.Entries) {
                            if ([string]::IsNullOrEmpty($entry.Name)) {
                                continue
                            }
                            $normalized = $entry.FullName.Replace('\', '/')
                            $slash = $normalized.IndexOf('/')
                            if ($slash -le 0) {
                                continue
                            }
                            $top = $normalized.Substring(0, $slash)
                            $matchesSet = $top.Equals($SetId, [System.StringComparison]::OrdinalIgnoreCase) -or
                                (($top + "m72").Equals($SetId, [System.StringComparison]::OrdinalIgnoreCase))
                            if (-not $matchesSet) {
                                continue
                            }
                            $relative = $normalized.Substring($slash + 1)
                            if ([string]::IsNullOrWhiteSpace($relative) -or $relative.EndsWith('/')) {
                                continue
                            }
                            $outEntry = $outputArchive.CreateEntry($relative, [System.IO.Compression.CompressionLevel]::Optimal)
                            $entryStream = $entry.Open()
                            try {
                                $outStream = $outEntry.Open()
                                try {
                                    $entryStream.CopyTo($outStream)
                                } finally {
                                    $outStream.Dispose()
                                }
                            } finally {
                                $entryStream.Dispose()
                            }
                            $copied += 1
                        }
                    } finally {
                        $outputArchive.Dispose()
                    }
                } finally {
                    $outputStream.Dispose()
                }
            } finally {
                $inputArchive.Dispose()
            }
        } finally {
            $inputStream.Dispose()
        }
    } catch {
        if (Test-Path -LiteralPath $subsetPath -PathType Leaf) {
            Remove-Item -LiteralPath $subsetPath -Force
        }
        return $null
    }
    if ($copied -eq 0) {
        if (Test-Path -LiteralPath $subsetPath -PathType Leaf) {
            Remove-Item -LiteralPath $subsetPath -Force
        }
        return $null
    }
    return $subsetPath
}

function Add-M72Candidate {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[object]]$Candidates,
        [Parameter(Mandatory = $true)][string]$Set,
        [Parameter(Mandatory = $true)][int]$Rank
    )
    foreach ($candidate in $Candidates) {
        if ($candidate.Set -ieq $Set) {
            if ($Rank -lt $candidate.Rank) {
                $candidate.Rank = $Rank
            }
            return
        }
    }
    $Candidates.Add([pscustomobject]@{ Set = $Set; Rank = $Rank })
}

function Get-M72RomCandidateSetIds {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$ManifestIds
    )
    $candidates = [System.Collections.Generic.List[object]]::new()
    if ($IncludeAllZips) {
        Add-M72Candidate -Candidates $candidates -Set ([System.IO.Path]::GetFileNameWithoutExtension($Path)) -Rank 0
        return @($candidates)
    }
    if (Test-Path -LiteralPath $Path -PathType Container) {
        $stem = [System.IO.Path]::GetFileName($Path)
    } else {
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    }
    $exactRank = 0
    if (Test-Path -LiteralPath $Path -PathType Container) {
        $exactRank = 3
    }
    if ($ManifestIds.Contains($stem)) {
        Add-M72Candidate -Candidates $candidates -Set $stem -Rank $exactRank
    }
    $m72SuffixedStem = $stem + "m72"
    if ($ManifestIds.Contains($m72SuffixedStem)) {
        Add-M72Candidate -Candidates $candidates -Set $m72SuffixedStem -Rank ($exactRank + 1)
    }
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $nestedSet = Get-M72NestedZipSetId -Path $Path -ManifestIds $ManifestIds
        if (-not [string]::IsNullOrWhiteSpace($nestedSet)) {
            Add-M72Candidate -Candidates $candidates -Set $nestedSet -Rank 0
        }
        $collectionSets = @(Get-M72CollectionZipSetIds -Path $Path -ManifestIds $ManifestIds)
        if ($collectionSets.Count -gt 0) {
            if (-not $ManifestIds.Contains($stem)) {
                $candidates.Clear()
            }
        }
        foreach ($collectionSet in $collectionSets) {
            Add-M72Candidate -Candidates $candidates -Set $collectionSet -Rank 2
        }
    }
    return @($candidates)
}

function Get-M72RomCandidateSetId {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$ManifestIds
    )
    $candidates = @(Get-M72RomCandidateSetIds -Path $Path -ManifestIds $ManifestIds | Sort-Object Rank, Set)
    if ($candidates.Count -eq 0) {
        return $null
    }
    return $candidates[0].Set
}

function New-M72RomGroups {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Paths,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$ManifestIds
    )

    $groups = [ordered]@{}
    foreach ($romPath in @($Paths | Sort-Object -Unique)) {
        $candidates = @(Get-M72RomCandidateSetIds -Path $romPath -ManifestIds $ManifestIds)
        if ($candidates.Count -eq 0) {
            Write-Warning "Irem M72 ROM path does not identify a checked-in set: $romPath"
            continue
        }
        foreach ($candidate in $candidates) {
            $setId = $candidate.Set
            $key = $setId.ToLowerInvariant()
            if (-not $groups.Contains($key)) {
                $groups[$key] = [pscustomobject]@{
                    Set = $setId
                    Paths = [System.Collections.Generic.List[string]]::new()
                    Ranks = [System.Collections.Generic.Dictionary[string, int]]::new([System.StringComparer]::OrdinalIgnoreCase)
                }
            }
            if (-not $groups[$key].Ranks.ContainsKey($romPath)) {
                $groups[$key].Paths.Add($romPath)
                $groups[$key].Ranks[$romPath] = $candidate.Rank
            } elseif ($candidate.Rank -lt $groups[$key].Ranks[$romPath]) {
                $groups[$key].Ranks[$romPath] = $candidate.Rank
            }
        }
    }

    return @($groups.Values)
}

function Add-M72RomArguments {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$Arguments,
        [Parameter(Mandatory = $true)][string[]]$RomPaths
    )
    foreach ($romPath in $RomPaths) {
        $Arguments.Add("--rom")
        $Arguments.Add($romPath)
    }
}

function Add-M72ParentGroupSources {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Groups,
        [Parameter(Mandatory = $true)][System.Collections.Generic.Dictionary[string, string]]$Parents
    )

    $bySet = @{}
    foreach ($group in $Groups) {
        $bySet[$group.Set.ToLowerInvariant()] = $group
    }
    foreach ($group in $Groups) {
        if (-not $Parents.ContainsKey($group.Set)) {
            continue
        }
        $parent = $Parents[$group.Set]
        $parentKey = $parent.ToLowerInvariant()
        if (-not $bySet.ContainsKey($parentKey)) {
            continue
        }
        foreach ($parentPath in $bySet[$parentKey].Paths) {
            $parentRank = $bySet[$parentKey].Ranks[$parentPath] + 10
            if (-not $group.Ranks.ContainsKey($parentPath)) {
                $group.Paths.Add($parentPath)
                $group.Ranks[$parentPath] = $parentRank
            } elseif ($parentRank -lt $group.Ranks[$parentPath]) {
                $group.Ranks[$parentPath] = $parentRank
            }
        }
    }
}

function Read-PpmToken {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][ref]$Index
    )
    while ($Index.Value -lt $Bytes.Length) {
        $b = $Bytes[$Index.Value]
        if ($b -eq 35) {
            while ($Index.Value -lt $Bytes.Length -and $Bytes[$Index.Value] -ne 10) {
                $Index.Value += 1
            }
            continue
        }
        if ($b -ne 9 -and $b -ne 10 -and $b -ne 13 -and $b -ne 32) {
            break
        }
        $Index.Value += 1
    }
    $start = $Index.Value
    while ($Index.Value -lt $Bytes.Length) {
        $b = $Bytes[$Index.Value]
        if ($b -eq 9 -or $b -eq 10 -or $b -eq 13 -or $b -eq 32) {
            break
        }
        $Index.Value += 1
    }
    return [System.Text.Encoding]::ASCII.GetString($Bytes, $start, $Index.Value - $start)
}

function Test-PpmHasLitPixel {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $index = 0
    $magic = Read-PpmToken -Bytes $bytes -Index ([ref]$index)
    $width = Read-PpmToken -Bytes $bytes -Index ([ref]$index)
    $height = Read-PpmToken -Bytes $bytes -Index ([ref]$index)
    $max = Read-PpmToken -Bytes $bytes -Index ([ref]$index)
    if ($magic -ne "P6" -or [int]$width -le 0 -or [int]$height -le 0 -or [int]$max -ne 255) {
        return $false
    }
    while ($index -lt $bytes.Length) {
        $b = $bytes[$index]
        $index += 1
        if ($b -eq 9 -or $b -eq 10 -or $b -eq 13 -or $b -eq 32) {
            break
        }
    }
    for ($i = $index; $i -lt $bytes.Length; ++$i) {
        if ($bytes[$i] -ne 0) {
            return $true
        }
    }
    return $false
}

function Read-U32Le {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        return 0
    }
    return [uint32](([uint32]$Bytes[$Offset]) -bor
        (([uint32]$Bytes[($Offset + 1)]) -shl 8) -bor
        (([uint32]$Bytes[($Offset + 2)]) -shl 16) -bor
        (([uint32]$Bytes[($Offset + 3)]) -shl 24))
}

function Test-WavHasNonZeroPcm {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 12) {
        return $false
    }
    $riff = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
    $wave = [System.Text.Encoding]::ASCII.GetString($bytes, 8, 4)
    if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
        return $false
    }
    $offset = 12
    while ($offset + 8 -le $bytes.Length) {
        $tag = [System.Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $size = [int](Read-U32Le -Bytes $bytes -Offset ($offset + 4))
        $payload = $offset + 8
        if ($size -lt 0 -or $payload + $size -gt $bytes.Length) {
            return $false
        }
        if ($tag -eq "data") {
            for ($i = $payload; $i -lt ($payload + $size); ++$i) {
                if ($bytes[$i] -ne 0) {
                    return $true
                }
            }
            return $false
        }
        $offset = $payload + $size
        if (($size % 2) -ne 0) {
            $offset += 1
        }
    }
    return $false
}

function Get-M72AudioProofFrames {
    param(
        [Parameter(Mandatory = $true)][string]$SetId,
        [Parameter(Mandatory = $true)][int]$DefaultFrames
    )
    switch -Regex ($SetId) {
        "^(airduelm72|airdueljm72)$" { return [Math]::Max($DefaultFrames, 1800) }
        default { return $DefaultFrames }
    }
}

if ($Frames -le 0) {
    throw "-Frames must be positive."
}
$fallbackFrameValues = @(ConvertTo-PositiveIntList -Values $FallbackFrames -Name "-FallbackFrames")
if ($AudioFrames -le 0) {
    throw "-AudioFrames must be positive."
}

$roms = [System.Collections.Generic.List[string]]::new()
foreach ($path in Split-CommaList $Rom) {
    Add-RomPath -Paths $roms -Path $path
}
foreach ($name in @("MNEMOS_M72_RTYPE_SET", "MNEMOS_M72_PROTECTED_SET", "MNEMOS_M72_PROTECTED_MCU_SET", "MNEMOS_M72_VERTICAL_SET")) {
    Add-RomPath -Paths $roms -Path ([Environment]::GetEnvironmentVariable($name))
}

$romDirs = [System.Collections.Generic.List[string]]::new()
foreach ($dirValue in Split-CommaList $RomDir) {
    foreach ($dirPath in Split-PathList $dirValue) {
        $romDirs.Add($dirPath)
    }
}
if ($romDirs.Count -eq 0) {
    foreach ($dirPath in Split-PathList ([Environment]::GetEnvironmentVariable("MNEMOS_M72_SET_DIR"))) {
        $romDirs.Add($dirPath)
    }
}

$manifestIds = Get-M72ManifestSetIds
$manifestParents = Get-M72ManifestParents
if ($romDirs.Count -gt 0) {
    foreach ($dirPath in $romDirs) {
        $resolvedDir = Resolve-RepoPath $dirPath
        if (-not (Test-Path -LiteralPath $resolvedDir -PathType Container)) {
            Write-Warning "Irem M72 ROM directory not found: $dirPath"
            continue
        }
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
            if ($null -ne (Get-M72RomCandidateSetId -Path $zip.FullName -ManifestIds $manifestIds)) {
                $roms.Add($zip.FullName)
            }
        }
        $dirArgs = @{
            LiteralPath = $resolvedDir
            Directory = $true
            ErrorAction = "SilentlyContinue"
        }
        if ($Recurse) {
            $dirArgs.Recurse = $true
        }
        foreach ($dir in Get-ChildItem @dirArgs | Sort-Object FullName) {
            if ($null -ne (Get-M72RomCandidateSetId -Path $dir.FullName -ManifestIds $manifestIds)) {
                $roms.Add($dir.FullName)
            }
        }
    }
}

$extra = [Environment]::GetEnvironmentVariable("MNEMOS_M72_EXTRA_ROMS")
if (-not [string]::IsNullOrWhiteSpace($extra)) {
    foreach ($path in $extra.Split([System.IO.Path]::PathSeparator, [System.StringSplitOptions]::RemoveEmptyEntries)) {
        Add-RomPath -Paths $roms -Path $path
    }
}

$romGroups = @(New-M72RomGroups -Paths @($roms) -ManifestIds $manifestIds)
Add-M72ParentGroupSources -Groups $romGroups -Parents $manifestParents
$requestedSets = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($setValue in Split-CommaList $Set) {
    if (-not $manifestIds.Contains($setValue)) {
        Write-Warning "Irem M72 set filter is not a checked-in manifest: $setValue"
        continue
    }
    [void]$requestedSets.Add($setValue)
}
if ($requestedSets.Count -gt 0) {
    $filteredGroups = @($romGroups | Where-Object { $requestedSets.Contains($_.Set) })
    foreach ($setValue in $requestedSets) {
        $matched = @($filteredGroups | Where-Object { $_.Set -ieq $setValue }).Count -gt 0
        if (-not $matched) {
            Write-Warning "Irem M72 set filter did not match discovered media: $setValue"
        }
    }
    $romGroups = $filteredGroups
}
if ($MaxSets -gt 0) {
    $romGroups = @($romGroups | Select-Object -First $MaxSets)
}

if ($romGroups.Count -eq 0) {
    Write-Host "No Irem M72 ROMs configured; set MNEMOS_M72_RTYPE_SET, MNEMOS_M72_PROTECTED_SET, MNEMOS_M72_VERTICAL_SET, or MNEMOS_M72_SET_DIR to run this gate. If -Set was used, no requested set was discovered." -ForegroundColor DarkGray
    exit 0
}

$buildRoot = Resolve-RepoPath $BuildDir
$player = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "mnemos_player.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($player)) {
    throw "mnemos_player.exe not found under '$buildRoot'. Build mnemos_player first."
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outDir = Join-Path $repoRoot "build/scratch/irem-m72-corpus/$stamp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
$index = 0
foreach ($romGroup in $romGroups) {
    $index += 1
    $setId = $romGroup.Set
    $setOut = Join-Path $outDir ("{0:D3}-{1}" -f $index, $setId)
    New-Item -ItemType Directory -Force -Path $setOut | Out-Null

    $effectiveSources = [System.Collections.Generic.List[object]]::new()
    $parentSetId = $null
    if ($manifestParents.ContainsKey($setId)) {
        $parentSetId = $manifestParents[$setId]
    }
    foreach ($sourcePath in $romGroup.Paths) {
        $rank = $romGroup.Ranks[$sourcePath]
        $collectionSets = @()
        if ((Test-Path -LiteralPath $sourcePath -PathType Leaf) -and
            ([System.IO.Path]::GetExtension($sourcePath) -ieq ".zip")) {
            $collectionSets = @(Get-M72CollectionZipSetIds -Path $sourcePath -ManifestIds $manifestIds)
        }

        $collectionHasSet = $false
        $collectionHasParent = $false
        foreach ($collectionSet in $collectionSets) {
            if ($collectionSet.Equals($setId, [System.StringComparison]::OrdinalIgnoreCase)) {
                $collectionHasSet = $true
            }
            if (-not [string]::IsNullOrWhiteSpace($parentSetId) -and
                $collectionSet.Equals($parentSetId, [System.StringComparison]::OrdinalIgnoreCase)) {
                $collectionHasParent = $true
            }
        }
        $sourceStemMatchesSet = (Test-Path -LiteralPath $sourcePath -PathType Leaf) -and
            ([System.IO.Path]::GetFileNameWithoutExtension($sourcePath).Equals($setId, [System.StringComparison]::OrdinalIgnoreCase))
        $sourceStemMatchesParent = (-not [string]::IsNullOrWhiteSpace($parentSetId)) -and
            (Test-Path -LiteralPath $sourcePath -PathType Leaf) -and
            ([System.IO.Path]::GetFileNameWithoutExtension($sourcePath).Equals($parentSetId, [System.StringComparison]::OrdinalIgnoreCase))

        if ($collectionHasParent) {
            $parentSubset = New-M72CollectionSubsetZip -Path $sourcePath -SetId $parentSetId -OutDir $outDir
            if (-not [string]::IsNullOrWhiteSpace($parentSubset)) {
                $effectiveSources.Add([pscustomobject]@{ Path = $parentSubset; Rank = 10; Source = $sourcePath }) | Out-Null
            }
        }
        if ($collectionHasSet) {
            $subset = New-M72CollectionSubsetZip -Path $sourcePath -SetId $setId -OutDir $outDir
            if (-not [string]::IsNullOrWhiteSpace($subset)) {
                $effectiveSources.Add([pscustomobject]@{ Path = $subset; Rank = 0; Source = $sourcePath }) | Out-Null
                if ($sourceStemMatchesParent) {
                    $effectiveSources.Add([pscustomobject]@{ Path = $sourcePath; Rank = 10; Source = $sourcePath }) | Out-Null
                }
                continue
            }
        }
        if (($collectionSets.Count -gt 0) -and (-not $collectionHasSet) -and (-not $sourceStemMatchesSet)) {
            continue
        }
        $effectiveSources.Add([pscustomobject]@{ Path = $sourcePath; Rank = $rank; Source = $sourcePath }) | Out-Null
    }
    $romPaths = @($effectiveSources |
        Sort-Object Rank, Path -Unique |
        Select-Object -ExpandProperty Path)

    Write-Host ("[irem_m72] {0} ({1} source(s))" -f $setId, $romPaths.Count) -ForegroundColor Cyan

    $frameAttempts = [System.Collections.Generic.List[int]]::new()
    foreach ($frame in @($Frames) + $fallbackFrameValues) {
        if (-not $frameAttempts.Contains($frame)) {
            $frameAttempts.Add($frame)
        }
    }

    $statePath = $null
    $saveLog = $null
    $loadLog = $null
    $screenshotPath = $null
    $audioBase = $null
    $audioLog = $null
    $audioPath = $null
    $saveExit = $null
    $loadExit = $null
    $audioExit = $null
    $litScreenshot = $false
    $renderedAudioNonZero = -not $RequireRenderedAudio
    $passed = $false
    $passedFrames = $null
    $audioProofFrames = Get-M72AudioProofFrames -SetId $setId -DefaultFrames $AudioFrames
    $mediaIssues = @()
    $resolvedSet = $setId
    $resolvedFrom = $null

    foreach ($attemptFrames in $frameAttempts) {
        $suffix = if ($attemptFrames -eq $Frames) { "" } else { ".f$attemptFrames" }
        $statePath = Join-Path $setOut "$setId$suffix.mns"
        $saveLog = Join-Path $setOut "$setId$suffix.save.log"
        $loadLog = Join-Path $setOut "$setId$suffix.load.log"
        $screenshotPath = Join-Path $setOut "$setId$suffix.after-load.ppm"
        $audioBase = Join-Path $setOut "$setId$suffix.audio"
        $audioLog = Join-Path $setOut "$setId$suffix.audio.log"
        $audioPath = "$audioBase.rendered.wav"

        $saveArgs = [System.Collections.Generic.List[string]]::new()
        $saveArgs.Add("--system")
        $saveArgs.Add("irem_m72")
        Add-M72RomArguments -Arguments $saveArgs -RomPaths $romPaths
        foreach ($arg in @(
            "--frames", $attemptFrames.ToString(),
            "--press", "start@1+2",
            "--press", "select@2+2",
            "--press", "service@3+2",
            "--save-state", $statePath
        )) {
            $saveArgs.Add($arg)
        }
        $saveExit = Invoke-Player -Player $player -LogPath $saveLog -Arguments $saveArgs.ToArray()

        $loadExit = $null
        $litScreenshot = $false
        if ($saveExit -eq 0) {
            $loadArgs = [System.Collections.Generic.List[string]]::new()
            $loadArgs.Add("--system")
            $loadArgs.Add("irem_m72")
            Add-M72RomArguments -Arguments $loadArgs -RomPaths $romPaths
            foreach ($arg in @(
                "--load-state", $statePath,
                "--frames", "1",
                "--screenshot", $screenshotPath
            )) {
                $loadArgs.Add($arg)
            }
            $loadExit = Invoke-Player -Player $player -LogPath $loadLog -Arguments $loadArgs.ToArray()
            if ($loadExit -eq 0) {
                $litScreenshot = Test-PpmHasLitPixel -Path $screenshotPath
            }
        }

        $audioExit = $null
        $renderedAudioNonZero = -not $RequireRenderedAudio
        if ($RequireRenderedAudio -and $saveExit -eq 0) {
            $audioArgs = [System.Collections.Generic.List[string]]::new()
            $audioArgs.Add("--system")
            $audioArgs.Add("irem_m72")
            Add-M72RomArguments -Arguments $audioArgs -RomPaths $romPaths
            foreach ($arg in @(
                "--press", "start@1+2",
                "--press", "select@2+2",
                "--press", "service@3+2",
                "--extract-audio", $audioBase,
                "--extract-frames", $audioProofFrames.ToString()
            )) {
                $audioArgs.Add($arg)
            }
            $audioExit = Invoke-Player -Player $player -LogPath $audioLog -Arguments $audioArgs.ToArray()
            if ($audioExit -eq 0) {
                $renderedAudioNonZero = Test-WavHasNonZeroPcm -Path $audioPath
            }
        }

        $mediaIssues = @(Get-M72MediaValidationIssues -LogPaths @($saveLog, $loadLog, $audioLog))
        $resolution = Get-M72ResolvedSetFromLog -LogPaths @($saveLog, $loadLog) -DefaultSet $setId
        $resolvedSet = $resolution.Set
        $resolvedFrom = $resolution.InferredFrom
        $mediaClean = ($mediaIssues.Count -eq 0)
        if (-not $mediaClean) {
            if ($frameAttempts.Count -gt 1) {
                Write-Host ("  [media] {0} has validation issues; skipping longer frame fallbacks" -f $setId) -ForegroundColor DarkYellow
            }
            break
        }
        $passed = ($saveExit -eq 0 -and $loadExit -eq 0 -and (Test-Path -LiteralPath $statePath) -and
            (Test-Path -LiteralPath $screenshotPath) -and $litScreenshot -and $mediaClean -and
            $renderedAudioNonZero)
        if ($passed) {
            $passedFrames = $attemptFrames
            break
        }
        if ($attemptFrames -ne $frameAttempts[$frameAttempts.Count - 1]) {
            Write-Host ("  [retry] {0} frame {1} did not produce a lit post-load screenshot" -f $setId, $attemptFrames) -ForegroundColor DarkYellow
        }
    }

    $results.Add([pscustomobject]@{
        set = $setId
        resolved_set = $resolvedSet
        resolved_from = $resolvedFrom
        rom = $romPaths[0]
        roms = @($romPaths)
        frames = $passedFrames
        attempted_frames = @($frameAttempts)
        save_exit = $saveExit
        load_exit = $loadExit
        audio_exit = $audioExit
        audio_frames = $audioProofFrames
        screenshot_lit = $litScreenshot
        audio_required = [bool]$RequireRenderedAudio
        rendered_audio_nonzero = $renderedAudioNonZero
        media_clean = ($mediaIssues.Count -eq 0)
        passed = $passed
        state = $statePath
        screenshot = $screenshotPath
        rendered_audio = $audioPath
        save_log = $saveLog
        load_log = $loadLog
        audio_log = $audioLog
        media_issues = @($mediaIssues)
    })
}

$summaryPath = Join-Path $outDir "summary.json"
$results | ConvertTo-Json -Depth 4 | Set-Content -Path $summaryPath -Encoding utf8

$failed = @($results | Where-Object { -not $_.passed })
Write-Host ("Irem M72 corpus smoke: {0}/{1} passed; summary: {2}" -f ($results.Count - $failed.Count), $results.Count, $summaryPath)
if ($failed.Count -gt 0) {
    foreach ($row in $failed) {
        Write-Host ("  [fail] {0} resolved={1} save={2} load={3} lit={4} audio={5} media_clean={6}" -f $row.set, $row.resolved_set, $row.save_exit, $row.load_exit, $row.screenshot_lit, $row.rendered_audio_nonzero, $row.media_clean) -ForegroundColor Red
        foreach ($issue in @($row.media_issues) | Select-Object -First 3) {
            Write-Host ("    {0}" -f $issue) -ForegroundColor DarkYellow
        }
    }
    exit 1
}

exit 0
