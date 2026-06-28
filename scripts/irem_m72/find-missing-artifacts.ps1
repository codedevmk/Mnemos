#!/usr/bin/env pwsh
# CRC-backed Irem M72 artifact inventory helper.
#
# ROMs are never copied into the repository. This script scans caller-provided
# local folders or archives and writes only metadata under build/scratch.

param(
    [string[]]$Root = @(),
    [string[]]$Set = @(),
    [switch]$Recurse,
    [switch]$ScanAllSevenZipEntries,
    [int]$MaxNestedZipDepth = 3,
    [string]$MissingFromReport = "",
    [string]$Out = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression

Add-Type -TypeDefinition @"
using System;

public static class MnemosCrc32 {
    private static readonly UInt32[] Table = BuildTable();

    private static UInt32[] BuildTable() {
        var table = new UInt32[256];
        for (UInt32 i = 0; i < table.Length; ++i) {
            UInt32 crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = ((crc & 1U) != 0U) ? (0xEDB88320U ^ (crc >> 1)) : (crc >> 1);
            }
            table[i] = crc;
        }
        return table;
    }

    public static UInt32 Compute(byte[] bytes) {
        UInt32 crc = 0xFFFFFFFFU;
        foreach (byte value in bytes) {
            crc = Table[(crc ^ value) & 0xFFU] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFU;
    }
}
"@

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function ConvertTo-HexCrc {
    param([Parameter(Mandatory = $true)][uint32]$Value)
    return ("{0:x8}" -f $Value)
}

function ConvertFrom-TomlInteger {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($Value.Substring(2), 16)
    }
    return [Convert]::ToInt64($Value, 10)
}

function New-StringSet {
    return ,([System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase))
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

function Add-TargetAlias {
    param(
        [Parameter(Mandatory = $true)][object]$Target,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not [string]::IsNullOrWhiteSpace($Name)) {
        [void]$Target.alias_names.Add([System.IO.Path]::GetFileName($Name))
    }
}

function Read-M72ManifestTargets {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[string]]$WantedSets
    )

    $setId = [System.IO.Path]::GetFileNameWithoutExtension($ManifestPath)
    $targets = [System.Collections.Generic.List[object]]::new()
    $currentRegion = ""
    $insideSet = $false
    $insideRegion = $false
    $inFile = $false
    $fileName = ""
    $aliases = [System.Collections.Generic.List[string]]::new()
    $size = $null
    $crc = $null

    function Flush-File {
        if (-not $script:inFile) {
            return
        }
        if (-not [string]::IsNullOrWhiteSpace($script:fileName) -and
            $null -ne $script:size -and
            $null -ne $script:crc) {
            $aliasNames = New-StringSet
            [void]$aliasNames.Add($script:fileName)
            foreach ($alias in $script:aliases) {
                if (-not [string]::IsNullOrWhiteSpace($alias)) {
                    [void]$aliasNames.Add($alias)
                }
            }
            $script:targets.Add([pscustomobject]@{
                set = $script:setId
                region = $script:currentRegion
                name = $script:fileName
                aliases = @($script:aliases)
                alias_names = $aliasNames
                size = [int64]$script:size
                crc32 = [uint32]$script:crc
                crc32_hex = ConvertTo-HexCrc -Value ([uint32]$script:crc)
                matches = [System.Collections.Generic.List[object]]::new()
                name_hits = [System.Collections.Generic.List[object]]::new()
                related_hits = [System.Collections.Generic.List[object]]::new()
            })
        }
        $script:inFile = $false
        $script:fileName = ""
        $script:aliases = [System.Collections.Generic.List[string]]::new()
        $script:size = $null
        $script:crc = $null
    }

    $script:setId = $setId
    $script:targets = $targets
    $script:currentRegion = $currentRegion
    $script:insideSet = $insideSet
    $script:insideRegion = $insideRegion
    $script:inFile = $inFile
    $script:fileName = $fileName
    $script:aliases = $aliases
    $script:size = $size
    $script:crc = $crc

    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        if ($line -match '^\s*\[set\]\s*$') {
            Flush-File
            $script:insideSet = $true
            $script:insideRegion = $false
            continue
        }
        if ($line -match '^\s*\[\[region\]\]\s*$') {
            Flush-File
            $script:currentRegion = ""
            $script:insideSet = $false
            $script:insideRegion = $true
            continue
        }
        if ($line -match '^\s*\[\[region\.file\]\]\s*$') {
            Flush-File
            $script:inFile = $true
            $script:insideSet = $false
            $script:insideRegion = $false
            continue
        }
        if ($line -match '^\s*\[') {
            Flush-File
            $script:insideSet = $false
            $script:insideRegion = $false
            continue
        }
        if ($line -match '^\s*name\s*=\s*"([^"]+)"\s*$') {
            if ($script:inFile) {
                $script:fileName = $Matches[1]
            } elseif ($script:insideSet) {
                $script:setId = $Matches[1]
            } elseif ($script:insideRegion) {
                $script:currentRegion = $Matches[1]
            }
            continue
        }
        if ($script:inFile -and $line -match '^\s*aliases\s*=\s*\[(.*)\]\s*$') {
            $script:aliases.Clear()
            foreach ($match in [regex]::Matches($Matches[1], '"([^"]+)"')) {
                $script:aliases.Add($match.Groups[1].Value)
            }
            continue
        }
        if ($script:inFile -and $line -match '^\s*size\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*$') {
            $script:size = ConvertFrom-TomlInteger -Value $Matches[1]
            continue
        }
        if ($script:inFile -and $line -match '^\s*crc32\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*$') {
            $script:crc = [uint32](ConvertFrom-TomlInteger -Value $Matches[1])
            continue
        }
    }
    Flush-File

    if ($WantedSets.Count -gt 0 -and -not $WantedSets.Contains($script:setId)) {
        return @()
    }
    return @($targets)
}

function Add-Match {
    param(
        [Parameter(Mandatory = $true)][object]$Target,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Entry,
        [Parameter(Mandatory = $true)][int64]$Size,
        [Parameter(Mandatory = $true)][uint32]$Crc
    )
    $targetMatches = $Target.matches
    foreach ($existing in $targetMatches) {
        if ($existing.source -eq $Source -and $existing.entry -eq $Entry) {
            return
        }
    }
    $targetMatches.Add([pscustomobject]@{
        source = $Source
        entry = $Entry
        size = $Size
        crc32 = ConvertTo-HexCrc -Value $Crc
    })
}

function Add-NameHit {
    param(
        [Parameter(Mandatory = $true)][object]$Target,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Entry,
        [Parameter(Mandatory = $true)][int64]$Size,
        [Parameter(Mandatory = $true)][uint32]$Crc
    )
    foreach ($existing in $Target.name_hits) {
        if ($existing.source -eq $Source -and $existing.entry -eq $Entry) {
            return
        }
    }
    $Target.name_hits.Add([pscustomobject]@{
        source = $Source
        entry = $Entry
        size = $Size
        crc32 = ConvertTo-HexCrc -Value $Crc
    })
}

function Test-CandidateMentionsSet {
    param(
        [Parameter(Mandatory = $true)][object]$Target,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Entry
    )

    $combined = "{0}!{1}" -f $Source, $Entry
    foreach ($setAlias in Get-SetDirectoryAliases -SetId $Target.set) {
        if ($combined.IndexOf($setAlias, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            return $true
        }
    }
    return $false
}

function Test-McuLikeCandidate {
    param(
        [Parameter(Mandatory = $true)][object]$Target,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Entry
    )

    if (-not $Target.region.Equals("mcu", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    if (-not (Test-CandidateMentionsSet -Target $Target -Source $Source -Entry $Entry)) {
        return $false
    }
    $leaf = [System.IO.Path]::GetFileName($Entry)
    return ($leaf -match '(?i)(mcu|8751|80c31|c-pr|pr-|\.ic1$)')
}

function Add-RelatedHit {
    param(
        [Parameter(Mandatory = $true)][object]$Target,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Entry,
        [Parameter(Mandatory = $true)][int64]$Size,
        [Parameter(Mandatory = $true)][uint32]$Crc,
        [Parameter(Mandatory = $true)][string]$Reason
    )

    foreach ($existing in $Target.related_hits) {
        if ($existing.source -eq $Source -and $existing.entry -eq $Entry) {
            return
        }
    }
    if ($Target.related_hits.Count -ge 8) {
        return
    }
    $Target.related_hits.Add([pscustomobject]@{
        source = $Source
        entry = $Entry
        size = $Size
        crc32 = ConvertTo-HexCrc -Value $Crc
        reason = $Reason
    })
}

function Test-ShouldReadCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$EntryName,
        [int64]$Size
    )
    $leaf = [System.IO.Path]::GetFileName($EntryName)
    if ($script:targetNames.Contains($leaf)) {
        return $true
    }
    if ($Size -ge 0 -and $script:targetSizes.Contains($Size)) {
        return $true
    }
    return $false
}

function Compare-CandidateBytes {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Entry
    )
    $crc = [MnemosCrc32]::Compute($Bytes)
    $leaf = [System.IO.Path]::GetFileName($Entry)
    if ($script:targetsByCrc.ContainsKey($crc)) {
        foreach ($target in $script:targetsByCrc[$crc]) {
            Add-Match -Target $target -Source $Source -Entry $Entry -Size $Bytes.LongLength -Crc $crc
        }
    }
    if ($script:targetsByName.ContainsKey($leaf)) {
        foreach ($target in $script:targetsByName[$leaf]) {
            if ($target.crc32 -ne $crc) {
                Add-NameHit -Target $target -Source $Source -Entry $Entry -Size $Bytes.LongLength -Crc $crc
            }
        }
    }
    if ($script:targetsBySize.ContainsKey($Bytes.LongLength)) {
        foreach ($target in $script:targetsBySize[$Bytes.LongLength]) {
            if ($target.crc32 -eq $crc) {
                continue
            }
            if (Test-McuLikeCandidate -Target $target -Source $Source -Entry $Entry) {
                Add-RelatedHit -Target $target -Source $Source -Entry $Entry -Size $Bytes.LongLength -Crc $crc `
                    -Reason "same-size set-local MCU-like candidate with different CRC"
            }
        }
    }
}

function Read-AllBytesFromStream {
    param([Parameter(Mandatory = $true)][System.IO.Stream]$Stream)
    $memory = [System.IO.MemoryStream]::new()
    try {
        $Stream.CopyTo($memory)
        return $memory.ToArray()
    } finally {
        $memory.Dispose()
    }
}

function Get-TarArchiveEntries {
    param([Parameter(Mandatory = $true)][string]$Path)

    $verboseEntries = @()
    try {
        $verboseEntries = @(& tar -tvf $Path 2>$null)
    } catch {
        $verboseEntries = @()
    }
    if ($LASTEXITCODE -eq 0 -and $verboseEntries.Count -gt 0) {
        foreach ($line in $verboseEntries) {
            if ($line -match '^\S+\s+\S+\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\d+\s+\S+\s+(.+)$') {
                $entry = $Matches[2]
                [pscustomobject]@{
                    entry = $entry
                    size = [int64]$Matches[1]
                    is_directory = $line.StartsWith("d", [System.StringComparison]::Ordinal) -or
                        $entry.EndsWith("/", [System.StringComparison]::Ordinal)
                }
            }
        }
        return
    }

    $plainEntries = @()
    try {
        $plainEntries = @(& tar -tf $Path 2>$null)
    } catch {
        Write-Verbose ("Unable to list archive with tar: {0}" -f $Path)
        return
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Unable to list archive with tar: $Path"
        return
    }
    foreach ($entry in $plainEntries) {
        [pscustomobject]@{
            entry = $entry
            size = [int64]-1
            is_directory = $entry.EndsWith("/", [System.StringComparison]::Ordinal)
        }
    }
}

function Scan-ZipStream {
    param(
        [Parameter(Mandatory = $true)][System.IO.Stream]$Stream,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][int]$Depth
    )
    $archive = $null
    try {
        $archive = [System.IO.Compression.ZipArchive]::new($Stream, [System.IO.Compression.ZipArchiveMode]::Read, $true)
    } catch {
        Write-Verbose ("Unable to open ZIP archive: {0}" -f $Source)
        return
    }
    try {
        foreach ($entry in $archive.Entries) {
            if ([string]::IsNullOrEmpty($entry.Name)) {
                continue
            }
            $entryPath = ("{0}!{1}" -f $Source, $entry.FullName)
            $isNestedZip = $entry.Name.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)
            if ((Test-ShouldReadCandidate -EntryName $entry.Name -Size $entry.Length) -or
                ($isNestedZip -and $Depth -lt $MaxNestedZipDepth)) {
                $entryStream = $entry.Open()
                try {
                    $bytes = Read-AllBytesFromStream -Stream $entryStream
                    if ($isNestedZip -and $Depth -lt $MaxNestedZipDepth) {
                        $nestedStream = [System.IO.MemoryStream]::new($bytes, $false)
                        try {
                            Scan-ZipStream -Stream $nestedStream -Source $entryPath -Depth ($Depth + 1)
                        } finally {
                            $nestedStream.Dispose()
                        }
                    }
                    if (Test-ShouldReadCandidate -EntryName $entry.Name -Size $bytes.LongLength) {
                        Compare-CandidateBytes -Bytes $bytes -Source $Source -Entry $entry.FullName
                    }
                } finally {
                    $entryStream.Dispose()
                }
            }
        }
    } finally {
        $archive.Dispose()
    }
}

function Scan-ZipFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        Scan-ZipStream -Stream $stream -Source $Path -Depth 0
    } finally {
        $stream.Dispose()
    }
}

function Scan-SevenZipFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    foreach ($archiveEntry in @(Get-TarArchiveEntries -Path $Path)) {
        $entry = $archiveEntry.entry
        if ([string]::IsNullOrWhiteSpace($entry) -or $archiveEntry.is_directory) {
            continue
        }
        $leaf = [System.IO.Path]::GetFileName($entry)
        if (-not (Test-ShouldReadCandidate -EntryName $leaf -Size $archiveEntry.size) -and
            -not $ScanAllSevenZipEntries -and
            -not $entry.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("mnemos-m72-artifact-{0}.bin" -f [guid]::NewGuid().ToString("N"))
        try {
            try {
                & tar -xOf $Path $entry > $temp 2>$null
            } catch {
                Write-Verbose ("Skipping unreadable archive entry: {0}!{1}" -f $Path, $entry)
                continue
            }
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $temp -PathType Leaf)) {
                Write-Verbose ("Skipping unreadable archive entry: {0}!{1}" -f $Path, $entry)
                continue
            }
            $raw = [System.IO.File]::ReadAllBytes($temp)
            if ($entry.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
                $nested = [System.IO.MemoryStream]::new($raw, $false)
                try {
                    Scan-ZipStream -Stream $nested -Source ("{0}!{1}" -f $Path, $entry) -Depth 1
                } catch {
                    # Ignore non-ZIP false positives; exact entries are checked below.
                } finally {
                    $nested.Dispose()
                }
            }
            if ($ScanAllSevenZipEntries -or
                (Test-ShouldReadCandidate -EntryName $leaf -Size $raw.LongLength)) {
                Compare-CandidateBytes -Bytes $raw -Source $Path -Entry $entry
            }
        } finally {
            if (Test-Path -LiteralPath $temp -PathType Leaf) {
                Remove-Item -LiteralPath $temp -Force
            }
        }
    }
}

function Scan-PlainFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $info = Get-Item -LiteralPath $Path
    if (-not (Test-ShouldReadCandidate -EntryName $info.Name -Size $info.Length)) {
        return
    }
    Compare-CandidateBytes -Bytes ([System.IO.File]::ReadAllBytes($Path)) -Source $Path -Entry $info.Name
}

function Scan-Path {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (Test-Path -LiteralPath $Path -PathType Container) {
        $args = @{
            LiteralPath = $Path
            File = $true
            ErrorAction = "SilentlyContinue"
        }
        if ($Recurse) {
            $args.Recurse = $true
        }
        foreach ($file in Get-ChildItem @args) {
            Scan-Path -Path $file.FullName
        }
        return
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Write-Warning "Artifact root not found: $Path"
        return
    }
    $extension = [System.IO.Path]::GetExtension($Path)
    if ($extension.Equals(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
        Scan-ZipFile -Path $Path
    } elseif ($extension.Equals(".7z", [System.StringComparison]::OrdinalIgnoreCase)) {
        Scan-SevenZipFile -Path $Path
    } else {
        Scan-PlainFile -Path $Path
    }
}

function Get-SetDirectoryAliases {
    param([Parameter(Mandatory = $true)][string]$SetId)

    $aliases = [System.Collections.Generic.List[string]]::new()
    $aliases.Add($SetId)
    if ($SetId.EndsWith("m72", [System.StringComparison]::OrdinalIgnoreCase) -and
        $SetId.Length -gt 3) {
        $aliases.Add($SetId.Substring(0, $SetId.Length - 3))
    }
    return @($aliases | Sort-Object -Unique)
}

function Get-SuggestedMissingLocations {
    param(
        [Parameter(Mandatory = $true)][object]$Target,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Roots
    )

    $locations = [System.Collections.Generic.List[string]]::new()
    $setAliases = @(Get-SetDirectoryAliases -SetId $Target.set)
    $fileAliases = [System.Collections.Generic.List[string]]::new()
    foreach ($alias in $Target.aliases) {
        if (-not [string]::IsNullOrWhiteSpace($alias) -and -not $fileAliases.Contains($alias)) {
            $fileAliases.Add($alias)
        }
    }
    if (-not $fileAliases.Contains($Target.name)) {
        $fileAliases.Add($Target.name)
    }
    foreach ($root in $Roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }
        foreach ($setAlias in $setAliases) {
            foreach ($baseDir in @($root, (Join-Path $root "m72"))) {
                $setDir = Join-Path $baseDir $setAlias
                if (Test-Path -LiteralPath $setDir -PathType Container) {
                    foreach ($fileAlias in $fileAliases) {
                        $candidate = Join-Path $setDir $fileAlias
                        if (-not $locations.Contains($candidate)) {
                            $locations.Add($candidate)
                        }
                    }
                }

                $zipPath = Join-Path $baseDir ("{0}.zip" -f $setAlias)
                if (Test-Path -LiteralPath $zipPath -PathType Leaf) {
                    foreach ($fileAlias in $fileAliases) {
                        $candidate = "{0}!{1}" -f $zipPath, $fileAlias
                        if (-not $locations.Contains($candidate)) {
                            $locations.Add($candidate)
                        }
                    }
                }
            }
        }
    }
    return @($locations)
}

$wantedSets = New-StringSet
foreach ($setId in Split-CommaList $Set) {
    if (-not [string]::IsNullOrWhiteSpace($setId)) {
        [void]$wantedSets.Add($setId)
    }
}

$manifestDir = Join-Path $repoRoot "src/manifests/irem_m72/games"
$allTargets = [System.Collections.Generic.List[object]]::new()
foreach ($manifest in Get-ChildItem -LiteralPath $manifestDir -Filter "*.toml" -File | Sort-Object Name) {
    foreach ($target in Read-M72ManifestTargets -ManifestPath $manifest.FullName -WantedSets $wantedSets) {
        $allTargets.Add($target)
    }
}
if ($allTargets.Count -eq 0) {
    throw "No Irem M72 manifest targets selected."
}

$missingFromReportPath = ""
if (-not [string]::IsNullOrWhiteSpace($MissingFromReport)) {
    $missingFromReportPath = Resolve-RepoPath $MissingFromReport
    if (-not (Test-Path -LiteralPath $missingFromReportPath -PathType Leaf)) {
        throw "Missing report not found: $MissingFromReport"
    }
    $missingReport = Get-Content -LiteralPath $missingFromReportPath -Raw | ConvertFrom-Json
    $missingKeys = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($target in @($missingReport.targets)) {
        if (-not [bool]$target.present) {
            [void]$missingKeys.Add(("{0}`t{1}`t{2}`t{3}" -f $target.set, $target.region,
                    $target.name, $target.crc32))
        }
    }
    $filteredTargets = [System.Collections.Generic.List[object]]::new()
    foreach ($target in $allTargets) {
        $key = "{0}`t{1}`t{2}`t{3}" -f $target.set, $target.region, $target.name,
            $target.crc32_hex
        if ($missingKeys.Contains($key)) {
            $filteredTargets.Add($target)
        }
    }
    $allTargets = $filteredTargets
    if ($allTargets.Count -eq 0) {
        throw "No selected targets matched missing entries from report: $MissingFromReport"
    }
}

$script:targetsByCrc = [System.Collections.Generic.Dictionary[uint32, System.Collections.Generic.List[object]]]::new()
$script:targetsByName = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[object]]]::new([System.StringComparer]::OrdinalIgnoreCase)
$script:targetsBySize = [System.Collections.Generic.Dictionary[int64, System.Collections.Generic.List[object]]]::new()
$script:targetNames = New-StringSet
$script:targetSizes = [System.Collections.Generic.HashSet[int64]]::new()
foreach ($target in $allTargets) {
    if (-not $script:targetsByCrc.ContainsKey($target.crc32)) {
        $script:targetsByCrc[$target.crc32] = [System.Collections.Generic.List[object]]::new()
    }
    $script:targetsByCrc[$target.crc32].Add($target)
    [void]$script:targetSizes.Add([int64]$target.size)
    if (-not $script:targetsBySize.ContainsKey([int64]$target.size)) {
        $script:targetsBySize[[int64]$target.size] = [System.Collections.Generic.List[object]]::new()
    }
    $script:targetsBySize[[int64]$target.size].Add($target)
    foreach ($aliasName in $target.alias_names) {
        [void]$script:targetNames.Add($aliasName)
        if (-not $script:targetsByName.ContainsKey($aliasName)) {
            $script:targetsByName[$aliasName] = [System.Collections.Generic.List[object]]::new()
        }
        $script:targetsByName[$aliasName].Add($target)
    }
}

if ($Root.Count -eq 0) {
    $envRoots = [Environment]::GetEnvironmentVariable("MNEMOS_M72_ARTIFACT_ROOTS")
    if (-not [string]::IsNullOrWhiteSpace($envRoots)) {
        $Root = @($envRoots.Split([System.IO.Path]::PathSeparator, [System.StringSplitOptions]::RemoveEmptyEntries))
    } elseif (Test-Path -LiteralPath "D:\emu\irem") {
        $Root = @("D:\emu\irem")
    }
}
if ($Root.Count -eq 0) {
    throw "No roots supplied. Pass -Root or set MNEMOS_M72_ARTIFACT_ROOTS."
}

$resolvedRoots = [System.Collections.Generic.List[string]]::new()
foreach ($path in $Root) {
    $resolved = Resolve-RepoPath $path
    if (Test-Path -LiteralPath $resolved) {
        $resolvedRoots.Add((Resolve-Path -LiteralPath $resolved).Path)
    } else {
        Write-Warning "Artifact root not found: $path"
    }
}
foreach ($path in $resolvedRoots) {
    Scan-Path -Path $path
}

$resultTargets = @($allTargets | ForEach-Object {
    [pscustomobject]@{
        set = $_.set
        region = $_.region
        name = $_.name
        aliases = @($_.aliases)
        size = $_.size
        crc32 = $_.crc32_hex
        present = ($_.matches.Count -gt 0)
        matches = @($_.matches)
        name_hits = @($_.name_hits)
        related_hits = @($_.related_hits)
        suggested_locations = @(Get-SuggestedMissingLocations -Target $_ -Roots @($resolvedRoots))
    }
})

$reportRunId = [guid]::NewGuid().ToString("N")
$summary = [pscustomobject]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    run_id = $reportRunId
    roots = @($resolvedRoots)
    selected_sets = @($allTargets | Select-Object -ExpandProperty set -Unique)
    missing_from_report = $missingFromReportPath
    target_count = $allTargets.Count
    present_count = @($resultTargets | Where-Object { $_.present }).Count
    missing_count = @($resultTargets | Where-Object { -not $_.present }).Count
    targets = $resultTargets
}

if ([string]::IsNullOrWhiteSpace($Out)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss-ffff"
    $runSuffix = $reportRunId.Substring(0, 8)
    $outDir = Join-Path $repoRoot "build/scratch/irem-m72-artifacts"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $Out = Join-Path $outDir "$stamp-$PID-$runSuffix.json"
} else {
    $Out = Resolve-RepoPath $Out
    $parent = Split-Path -Parent $Out
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding utf8
$missing = @($resultTargets | Where-Object { -not $_.present })
Write-Host ("Irem M72 artifact scan: {0}/{1} present; report: {2}" -f ($allTargets.Count - $missing.Count), $allTargets.Count, $Out)
foreach ($target in $missing | Select-Object -First 20) {
    $suggestion = @($target.suggested_locations | Select-Object -First 1)
    $suffix = if ($suggestion.Count -gt 0) { " suggested={0}" -f $suggestion[0] } else { "" }
    Write-Host ("  [missing] {0}:{1}:{2} crc={3} size={4}{5}" -f $target.set, $target.region, $target.name, $target.crc32, $target.size, $suffix) -ForegroundColor DarkYellow
    foreach ($nameHit in @($target.name_hits | Select-Object -First 3)) {
        Write-Host ("    [name-hit] {0}!{1} crc={2} size={3} (matching name or alias, wrong CRC)" -f $nameHit.source, $nameHit.entry, $nameHit.crc32, $nameHit.size) -ForegroundColor DarkYellow
    }
    foreach ($related in @($target.related_hits | Select-Object -First 3)) {
        Write-Host ("    [related] {0}!{1} crc={2} size={3} ({4})" -f $related.source, $related.entry, $related.crc32, $related.size, $related.reason) -ForegroundColor DarkYellow
    }
}
if ($missing.Count -gt 20) {
    Write-Host ("  ... {0} more missing artifact(s)" -f ($missing.Count - 20)) -ForegroundColor DarkYellow
}

if ($missing.Count -gt 0) {
    exit 1
}
exit 0
