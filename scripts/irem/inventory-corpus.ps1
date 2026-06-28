#!/usr/bin/env pwsh
# Metadata-only inventory for a local Irem arcade corpus.
#
# ROMs are never copied into the repository. This script records archive/folder
# names, board-bucket placement, nested set names, manifest matches, and current
# ZIP/folder loadability so board-family bring-up starts from auditable local
# evidence instead of folder-name assumptions.

param(
    [string[]]$Root = @(),
    [switch]$Recurse,
    [int]$MaxEntries = 24,
    [string]$Out = ""
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

function Get-IremBucketName {
    param([string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name)) {
        return ""
    }
    switch ($Name.ToLowerInvariant()) {
        "m10" { return "M10" }
        "m14" { return "M14" }
        "m15" { return "M15" }
        "m27" { return "M27" }
        "m47" { return "M47" }
        "m52" { return "M52" }
        "m57" { return "M57" }
        "m58" { return "M58" }
        "m62" { return "M62" }
        "m63" { return "M63" }
        "m72" { return "M72" }
        "m75" { return "M75" }
        "m78" { return "M78" }
        "m81" { return "M81" }
        "m82" { return "M82" }
        "m84" { return "M84" }
        "m85" { return "M85" }
        "m90" { return "M90" }
        "m92" { return "M92" }
        "m102" { return "M102" }
        "m107" { return "M107" }
        "i8751" { return "i8751" }
        "travrusa" { return "travrusa" }
        "for-delete" { return "for-delete" }
        "misc" { return "misc" }
        "non-irem" { return "non-irem" }
        default { return "" }
    }
}

function Test-IgnoredCorpusBucket {
    param([Parameter(Mandatory = $true)][string]$Bucket)
    return $Bucket -in @("for-delete", "misc", "non-irem")
}

function Get-RelativePathCompat {
    param(
        [Parameter(Mandatory = $true)][string]$RootPath,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $relativeMethod = [System.IO.Path].GetMethod("GetRelativePath", [type[]]@([string], [string]))
    if ($null -ne $relativeMethod) {
        return [System.IO.Path]::GetRelativePath($RootPath, $Path)
    }
    $rootFull = [System.IO.Path]::GetFullPath($RootPath)
    if (-not $rootFull.EndsWith([System.IO.Path]::DirectorySeparatorChar) -and
        -not $rootFull.EndsWith([System.IO.Path]::AltDirectorySeparatorChar)) {
        $rootFull += [System.IO.Path]::DirectorySeparatorChar
    }
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    $relativeUri = ([System.Uri]$rootFull).MakeRelativeUri([System.Uri]$pathFull).ToString()
    return [System.Uri]::UnescapeDataString($relativeUri).Replace(
        [System.IO.Path]::AltDirectorySeparatorChar,
        [System.IO.Path]::DirectorySeparatorChar)
}

function Get-BucketForPath {
    param(
        [Parameter(Mandatory = $true)][string]$RootPath,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $rootBucket = Get-IremBucketName (Split-Path -Leaf $RootPath)
    $relative = Get-RelativePathCompat -RootPath $RootPath -Path $Path
    if ([string]::IsNullOrWhiteSpace($relative) -or $relative.StartsWith("..")) {
        return $(if ($rootBucket.Length -gt 0) { $rootBucket } else { "root" })
    }

    $parts = @($relative -split "[\\/]" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($parts.Count -eq 0) {
        return $(if ($rootBucket.Length -gt 0) { $rootBucket } else { "root" })
    }

    if ($rootBucket.Length -gt 0) {
        return $rootBucket
    }

    $firstBucket = Get-IremBucketName $parts[0]
    if ($firstBucket.Length -gt 0) {
        return $firstBucket
    }
    return "root"
}

function Read-GameManifestIds {
    param([Parameter(Mandatory = $true)][string]$RelativeGamesDir)

    $ids = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $gamesDir = Join-Path $repoRoot $RelativeGamesDir
    if (-not (Test-Path -LiteralPath $gamesDir -PathType Container)) {
        return ,$ids
    }
    foreach ($manifest in Get-ChildItem -LiteralPath $gamesDir -Filter "*.toml" -File) {
        $ids.Add([System.IO.Path]::GetFileNameWithoutExtension($manifest.Name)) | Out-Null
    }
    return ,$ids
}

function Read-GameManifestParents {
    param([Parameter(Mandatory = $true)][string]$RelativeGamesDir)

    $parents = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $gamesDir = Join-Path $repoRoot $RelativeGamesDir
    if (-not (Test-Path -LiteralPath $gamesDir -PathType Container)) {
        return ,$parents
    }
    foreach ($manifest in Get-ChildItem -LiteralPath $gamesDir -Filter "*.toml" -File) {
        foreach ($line in Get-Content -LiteralPath $manifest.FullName) {
            if ($line -match '^\s*parent\s*=\s*"([^"]+)"') {
                $parents[[System.IO.Path]::GetFileNameWithoutExtension($manifest.Name)] = $Matches[1]
                break
            }
        }
    }
    return ,$parents
}

function Add-ManifestParents {
    param(
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.Dictionary[string, string]]$ParentMap
    )
    foreach ($setId in $ManifestIds) {
        if ($ParentMap.ContainsKey($setId) -and -not $script:manifestParentBySet.ContainsKey($setId)) {
            $script:manifestParentBySet[$setId] = $ParentMap[$setId]
        }
    }
}

function Get-ArchiveEntries {
    param([Parameter(Mandatory = $true)][string]$Path)
    $output = @(& tar -tf $Path 2>$null)
    if ($LASTEXITCODE -ne 0) {
        return @()
    }
    return @($output | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-SetIdFromPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [AllowEmptyCollection()][string[]]$NestedArchives
    )
    function Normalize-CopySuffixedSetId {
        param([Parameter(Mandatory = $true)][string]$Value)

        if ($Value -match '^[A-Za-z0-9_]+$') {
            return $Value
        }
        if ($Value -match '^([A-Za-z0-9_]+) \([0-9]+\)$') {
            return $Matches[1]
        }
        return $Value
    }

    if ($NestedArchives.Count -eq 1) {
        return Normalize-CopySuffixedSetId ([System.IO.Path]::GetFileNameWithoutExtension($NestedArchives[0]))
    }
    return Normalize-CopySuffixedSetId ([System.IO.Path]::GetFileNameWithoutExtension($Path))
}

function Get-LoadRouteForItem {
    param(
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Extension,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$NestedArchives
    )
    if ($Kind -eq "directory") {
        return "directory"
    }
    if ($Extension.Equals(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
        if ($NestedArchives.Count -eq 1) {
            return "single_inner_zip"
        }
        return "zip"
    }
    if ($Extension.Equals(".7z", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "metadata_only_7z"
    }
    return "unsupported"
}

function Test-MnemosLoadableRoute {
    param([Parameter(Mandatory = $true)][string]$LoadRoute)
    return $LoadRoute -in @("zip", "single_inner_zip", "directory")
}

function Get-TrackedFamilyName {
    param(
        [Parameter(Mandatory = $true)][bool]$M14Match,
        [Parameter(Mandatory = $true)][bool]$M15Match,
        [Parameter(Mandatory = $true)][bool]$M27Match,
        [Parameter(Mandatory = $true)][bool]$M47Match,
        [Parameter(Mandatory = $true)][bool]$M52Match,
        [Parameter(Mandatory = $true)][bool]$M58Match,
        [Parameter(Mandatory = $true)][bool]$M62Match,
        [Parameter(Mandatory = $true)][bool]$M63Match,
        [Parameter(Mandatory = $true)][bool]$M72Match,
        [Parameter(Mandatory = $true)][bool]$M75Match,
        [Parameter(Mandatory = $true)][bool]$M81Match,
        [Parameter(Mandatory = $true)][bool]$M82Match,
        [Parameter(Mandatory = $true)][bool]$M84Match,
        [Parameter(Mandatory = $true)][bool]$M90Match,
        [Parameter(Mandatory = $true)][bool]$M92Match,
        [Parameter(Mandatory = $true)][bool]$M107Match
    )
    $matches = [System.Collections.Generic.List[string]]::new()
    if ($M14Match) { $matches.Add("M14") }
    if ($M15Match) { $matches.Add("M15") }
    if ($M27Match) { $matches.Add("M27") }
    if ($M47Match) { $matches.Add("M47") }
    if ($M52Match) { $matches.Add("M52") }
    if ($M58Match) { $matches.Add("M58") }
    if ($M62Match) { $matches.Add("M62") }
    if ($M63Match) { $matches.Add("M63") }
    if ($M72Match) { $matches.Add("M72") }
    if ($M75Match) { $matches.Add("M75") }
    if ($M81Match) { $matches.Add("M81") }
    if ($M82Match) { $matches.Add("M82") }
    if ($M84Match) { $matches.Add("M84") }
    if ($M90Match) { $matches.Add("M90") }
    if ($M92Match) { $matches.Add("M92") }
    if ($M107Match) { $matches.Add("M107") }
    return ($matches -join ";")
}

function Get-ManifestParentForSet {
    param([Parameter(Mandatory = $true)][string]$SetId)
    if ($script:manifestParentBySet.ContainsKey($SetId)) {
        return $script:manifestParentBySet[$SetId]
    }
    return ""
}

function New-CorpusClassification {
    param(
        [string]$Classification = "",
        [string]$SourceDriver = "",
        [string]$SourceFamily = "",
        [string]$SourceOwner = "",
        [string]$MissingParentZip = "",
        [string]$NextAction = ""
    )
    return [pscustomobject]@{
        classification = $Classification
        source_driver = $SourceDriver
        source_family = $SourceFamily
        source_owner = $SourceOwner
        missing_parent_zip = $MissingParentZip
        next_action = $NextAction
    }
}

function Get-KnownCorpusClassification {
    param([Parameter(Mandatory = $true)][string]$SetId)
    switch ($SetId.ToLowerInvariant()) {
        "travrusa" {
            return New-CorpusClassification `
                -Classification "irem_parent_candidate" `
                -SourceDriver "irem/travrusa.cpp" `
                -SourceFamily "travrusa" `
                -SourceOwner "irem" `
                -NextAction "add_manifest_and_board_profile"
        }
        { $_ -in @("motorace", "travrusab", "travrusab2") } {
            return New-CorpusClassification `
                -Classification "irem_split_clone_parent_present" `
                -SourceDriver "irem/travrusa.cpp" `
                -SourceFamily "travrusa" `
                -SourceOwner "irem" `
                -NextAction "add_manifest_and_board_profile"
        }
        "headon" {
            return New-CorpusClassification `
                -Classification "non_irem_reference" `
                -SourceDriver "sega/vicdual.cpp" `
                -SourceFamily "headon" `
                -SourceOwner "non_irem" `
                -NextAction "move_or_ignore_non_irem_reference"
        }
        { $_ -in @("uniwars", "uniwarsa") } {
            return New-CorpusClassification `
                -Classification "non_irem_reference" `
                -SourceDriver "galaxian/galaxian.cpp" `
                -SourceFamily "uniwars" `
                -SourceOwner "non_irem" `
                -NextAction "move_or_ignore_non_irem_reference"
        }
        default {
            return New-CorpusClassification
        }
    }
}

function Get-SetRole {
    param(
        [Parameter(Mandatory = $true)][bool]$TrackedByMnemos,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$ManifestParent,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$BoardCandidateFamily
    )
    if ($TrackedByMnemos) {
        if ([string]::IsNullOrWhiteSpace($ManifestParent)) {
            return "parent_or_standalone"
        }
        return "clone_declares_parent"
    }
    if (-not [string]::IsNullOrWhiteSpace($BoardCandidateFamily)) {
        return "board_family_candidate"
    }
    return "unclassified"
}

function Get-ArchiveComposition {
    param(
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Extension,
        [Parameter(Mandatory = $true)][int]$EntryCount,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$NestedArchives
    )
    if ($Kind -eq "directory") {
        return "unpacked_folder"
    }
    if ($Extension.Equals(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
        if ($NestedArchives.Count -eq 1 -and $EntryCount -eq 1) {
            return "single_inner_zip_wrapper"
        }
        if ($NestedArchives.Count -gt 1) {
            return "collection_zip"
        }
        return "set_zip"
    }
    if ($Extension.Equals(".7z", [System.StringComparison]::OrdinalIgnoreCase)) {
        if ($NestedArchives.Count -eq 1 -and $EntryCount -eq 1) {
            return "metadata_only_single_inner_7z_wrapper"
        }
        if ($NestedArchives.Count -gt 1) {
            return "metadata_only_collection_7z"
        }
        return "metadata_only_set_7z"
    }
    return "unsupported_archive"
}

function Get-LoadReadiness {
    param(
        [Parameter(Mandatory = $true)][bool]$TrackedByMnemos,
        [Parameter(Mandatory = $true)][bool]$ContractOnly,
        [Parameter(Mandatory = $true)][bool]$LoadableByMnemos,
        [Parameter(Mandatory = $true)][string]$LoadRoute,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$BoardCandidateFamily
    )
    if ($ContractOnly) {
        return "tracked_contract_only"
    }
    if ($LoadableByMnemos) {
        return "direct_player_loadable"
    }
    if ($TrackedByMnemos -and $LoadRoute -eq "metadata_only_7z") {
        return "metadata_only_unpack_or_repack"
    }
    if ($TrackedByMnemos) {
        return "tracked_but_not_player_loadable"
    }
    if (-not [string]::IsNullOrWhiteSpace($BoardCandidateFamily)) {
        return "candidate_needs_manifest_or_board"
    }
    return "untracked"
}

function Get-BoardCandidateFamily {
    param(
        [Parameter(Mandatory = $true)][string]$Bucket,
        [Parameter(Mandatory = $true)][bool]$TrackedByMnemos
    )
    if ($TrackedByMnemos) {
        return ""
    }
    if ($Bucket -in @("M10", "M14", "M15", "M27", "M47", "M52", "M57", "M58", "M62", "M63", "M72", "M75", "M78", "M81", "M82", "M84", "M85", "M90", "M92", "M102", "M107", "i8751", "travrusa")) {
        return $Bucket
    }
    return ""
}

function Get-InventoryNextAction {
    param(
        [Parameter(Mandatory = $true)][bool]$TrackedByMnemos,
        [Parameter(Mandatory = $true)][bool]$ContractOnly,
        [Parameter(Mandatory = $true)][bool]$LoadableByMnemos,
        [Parameter(Mandatory = $true)][string]$LoadRoute,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$BoardCandidateFamily
    )
    if ($ContractOnly) {
        return "add_board_profile"
    }
    if ($LoadableByMnemos) {
        return "player_loadable"
    }
    if ($TrackedByMnemos -and $LoadRoute -eq "metadata_only_7z") {
        return "convert_or_unpack_for_player_load"
    }
    if ($TrackedByMnemos) {
        return "fix_current_loader_route"
    }
    if (-not [string]::IsNullOrWhiteSpace($BoardCandidateFamily)) {
        return "add_manifest_and_board_profile"
    }
    return "classify_or_sort_corpus_item"
}

function Get-EffectiveInventoryNextAction {
    param(
        [Parameter(Mandatory = $true)][string]$DefaultAction,
        [Parameter(Mandatory = $true)]$KnownClassification
    )
    if (-not [string]::IsNullOrWhiteSpace($KnownClassification.next_action)) {
        return $KnownClassification.next_action
    }
    return $DefaultAction
}

function New-ArchiveItem {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileInfo]$File,
        [Parameter(Mandatory = $true)][string]$Bucket,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M14ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M15ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M27ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M47ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M52ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M58ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M62ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M63ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M72ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M75ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M81ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M82ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M84ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M90ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M92ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M107ManifestIds
    )

    $entries = @(Get-ArchiveEntries -Path $File.FullName)
    $nestedArchives = @($entries |
        Where-Object { $_ -match '\.(zip|7z)$' } |
        ForEach-Object { [System.IO.Path]::GetFileName($_) } |
        Sort-Object -Unique)
    $setId = Get-SetIdFromPath -Path $File.FullName -NestedArchives $nestedArchives
    $m14Match = $M14ManifestIds.Contains($setId)
    $m15Match = $M15ManifestIds.Contains($setId)
    $m27Match = $M27ManifestIds.Contains($setId)
    $m47Match = $M47ManifestIds.Contains($setId)
    $m52Match = $M52ManifestIds.Contains($setId)
    $m58Match = $M58ManifestIds.Contains($setId)
    $m62Match = $M62ManifestIds.Contains($setId)
    $m63Match = $M63ManifestIds.Contains($setId)
    $m72Match = $M72ManifestIds.Contains($setId)
    $m75Match = $M75ManifestIds.Contains($setId)
    $m81Match = $M81ManifestIds.Contains($setId)
    $m82Match = $M82ManifestIds.Contains($setId)
    $m84Match = $M84ManifestIds.Contains($setId)
    $m90Match = $M90ManifestIds.Contains($setId)
    $m92Match = $M92ManifestIds.Contains($setId)
    $m107Match = $M107ManifestIds.Contains($setId)
    $ignoredBucket = Test-IgnoredCorpusBucket -Bucket $Bucket
    $trackedMatch = -not $ignoredBucket -and ($m14Match -or $m15Match -or $m27Match -or $m47Match -or $m52Match -or $m58Match -or $m62Match -or $m63Match -or $m72Match -or $m75Match -or $m81Match -or $m82Match -or $m84Match -or $m90Match -or $m92Match -or $m107Match)
    $loadRoute = Get-LoadRouteForItem -Kind "archive" -Extension $File.Extension -NestedArchives $nestedArchives
    $contractOnly = $trackedMatch -and ($m14Match -or $m27Match -or $m47Match -or $m62Match -or $m63Match)
    $loadableByMnemos = $trackedMatch -and (Test-MnemosLoadableRoute -LoadRoute $loadRoute)
    $supportedByMnemos = $loadableByMnemos -and -not $contractOnly
    $trackedFamily = Get-TrackedFamilyName -M14Match $m14Match -M15Match $m15Match -M27Match $m27Match -M47Match $m47Match -M52Match $m52Match -M58Match $m58Match -M62Match $m62Match -M63Match $m63Match -M72Match $m72Match -M75Match $m75Match -M81Match $m81Match -M82Match $m82Match -M84Match $m84Match -M90Match $m90Match -M92Match $m92Match -M107Match $m107Match
    $manifestParent = Get-ManifestParentForSet -SetId $setId
    $boardCandidateFamily = Get-BoardCandidateFamily -Bucket $Bucket -TrackedByMnemos $trackedMatch
    $archiveComposition = Get-ArchiveComposition -Kind "archive" -Extension $File.Extension -EntryCount $entries.Count -NestedArchives $nestedArchives
    $knownClassification = Get-KnownCorpusClassification -SetId $setId
    $defaultNextAction = Get-InventoryNextAction -TrackedByMnemos $trackedMatch -ContractOnly $contractOnly -LoadableByMnemos $loadableByMnemos -LoadRoute $loadRoute -BoardCandidateFamily $boardCandidateFamily

    return [pscustomobject]@{
        kind = "archive"
        bucket = $Bucket
        path = $File.FullName
        name = $File.Name
        extension = $File.Extension.ToLowerInvariant()
        size_bytes = $File.Length
        set_id = $setId
        m14_manifest_match = $m14Match
        m15_manifest_match = $m15Match
        m27_manifest_match = $m27Match
        m47_manifest_match = $m47Match
        m52_manifest_match = $m52Match
        m58_manifest_match = $m58Match
        m62_manifest_match = $m62Match
        m63_manifest_match = $m63Match
        m72_manifest_match = $m72Match
        m75_manifest_match = $m75Match
        m81_manifest_match = $m81Match
        m82_manifest_match = $m82Match
        m84_manifest_match = $m84Match
        m90_manifest_match = $m90Match
        m92_manifest_match = $m92Match
        m107_manifest_match = $m107Match
        tracked_by_mnemos = $trackedMatch
        tracked_family = $trackedFamily
        manifest_parent = $manifestParent
        set_role = Get-SetRole -TrackedByMnemos $trackedMatch -ManifestParent $manifestParent -BoardCandidateFamily $boardCandidateFamily
        load_route = $loadRoute
        archive_composition = $archiveComposition
        loadable_by_mnemos = $loadableByMnemos
        supported_by_mnemos = $supportedByMnemos
        load_readiness = Get-LoadReadiness -TrackedByMnemos $trackedMatch -ContractOnly $contractOnly -LoadableByMnemos $loadableByMnemos -LoadRoute $loadRoute -BoardCandidateFamily $boardCandidateFamily
        board_candidate_family = $boardCandidateFamily
        corpus_classification = $knownClassification.classification
        known_source_driver = $knownClassification.source_driver
        known_source_family = $knownClassification.source_family
        known_source_owner = $knownClassification.source_owner
        missing_parent_zip = $knownClassification.missing_parent_zip
        next_action = Get-EffectiveInventoryNextAction -DefaultAction $defaultNextAction -KnownClassification $knownClassification
        entry_count = $entries.Count
        nested_archives = @($nestedArchives)
        sample_entries = @($entries | Select-Object -First $MaxEntries)
    }
}

function New-DirectoryItem {
    param(
        [Parameter(Mandatory = $true)][System.IO.DirectoryInfo]$Directory,
        [Parameter(Mandatory = $true)][string]$Bucket,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M14ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M15ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M27ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M47ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M52ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M58ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M62ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M63ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M72ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M75ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M81ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M82ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M84ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M90ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M92ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M107ManifestIds
    )

    $files = @(Get-ChildItem -LiteralPath $Directory.FullName -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -notin @(".zip", ".7z") })
    if ($files.Count -eq 0) {
        return $null
    }

    $setId = $Directory.Name
    $m14Match = $M14ManifestIds.Contains($setId)
    $m15Match = $M15ManifestIds.Contains($setId)
    $m27Match = $M27ManifestIds.Contains($setId)
    $m47Match = $M47ManifestIds.Contains($setId)
    $m52Match = $M52ManifestIds.Contains($setId)
    $m58Match = $M58ManifestIds.Contains($setId)
    $m62Match = $M62ManifestIds.Contains($setId)
    $m63Match = $M63ManifestIds.Contains($setId)
    $m72Match = $M72ManifestIds.Contains($setId)
    $m75Match = $M75ManifestIds.Contains($setId)
    $m81Match = $M81ManifestIds.Contains($setId)
    $m82Match = $M82ManifestIds.Contains($setId)
    $m84Match = $M84ManifestIds.Contains($setId)
    $m90Match = $M90ManifestIds.Contains($setId)
    $m92Match = $M92ManifestIds.Contains($setId)
    $m107Match = $M107ManifestIds.Contains($setId)
    $ignoredBucket = Test-IgnoredCorpusBucket -Bucket $Bucket
    $trackedMatch = -not $ignoredBucket -and ($m14Match -or $m15Match -or $m27Match -or $m47Match -or $m52Match -or $m58Match -or $m62Match -or $m63Match -or $m72Match -or $m75Match -or $m81Match -or $m82Match -or $m84Match -or $m90Match -or $m92Match -or $m107Match)
    $loadRoute = Get-LoadRouteForItem -Kind "directory" -Extension "" -NestedArchives @()
    $contractOnly = $trackedMatch -and ($m14Match -or $m27Match -or $m47Match -or $m62Match -or $m63Match)
    $loadableByMnemos = $trackedMatch -and (Test-MnemosLoadableRoute -LoadRoute $loadRoute)
    $supportedByMnemos = $loadableByMnemos -and -not $contractOnly
    $trackedFamily = Get-TrackedFamilyName -M14Match $m14Match -M15Match $m15Match -M27Match $m27Match -M47Match $m47Match -M52Match $m52Match -M58Match $m58Match -M62Match $m62Match -M63Match $m63Match -M72Match $m72Match -M75Match $m75Match -M81Match $m81Match -M82Match $m82Match -M84Match $m84Match -M90Match $m90Match -M92Match $m92Match -M107Match $m107Match
    $manifestParent = Get-ManifestParentForSet -SetId $setId
    $boardCandidateFamily = Get-BoardCandidateFamily -Bucket $Bucket -TrackedByMnemos $trackedMatch
    $knownClassification = Get-KnownCorpusClassification -SetId $setId
    $defaultNextAction = Get-InventoryNextAction -TrackedByMnemos $trackedMatch -ContractOnly $contractOnly -LoadableByMnemos $loadableByMnemos -LoadRoute $loadRoute -BoardCandidateFamily $boardCandidateFamily
    return [pscustomobject]@{
        kind = "directory"
        bucket = $Bucket
        path = $Directory.FullName
        name = $Directory.Name
        extension = ""
        size_bytes = ($files | Measure-Object Length -Sum).Sum
        set_id = $setId
        m14_manifest_match = $m14Match
        m15_manifest_match = $m15Match
        m27_manifest_match = $m27Match
        m47_manifest_match = $m47Match
        m52_manifest_match = $m52Match
        m58_manifest_match = $m58Match
        m62_manifest_match = $m62Match
        m63_manifest_match = $m63Match
        m72_manifest_match = $m72Match
        m75_manifest_match = $m75Match
        m81_manifest_match = $m81Match
        m82_manifest_match = $m82Match
        m84_manifest_match = $m84Match
        m90_manifest_match = $m90Match
        m92_manifest_match = $m92Match
        m107_manifest_match = $m107Match
        tracked_by_mnemos = $trackedMatch
        tracked_family = $trackedFamily
        manifest_parent = $manifestParent
        set_role = Get-SetRole -TrackedByMnemos $trackedMatch -ManifestParent $manifestParent -BoardCandidateFamily $boardCandidateFamily
        load_route = $loadRoute
        archive_composition = Get-ArchiveComposition -Kind "directory" -Extension "" -EntryCount $files.Count -NestedArchives @()
        loadable_by_mnemos = $loadableByMnemos
        supported_by_mnemos = $supportedByMnemos
        load_readiness = Get-LoadReadiness -TrackedByMnemos $trackedMatch -ContractOnly $contractOnly -LoadableByMnemos $loadableByMnemos -LoadRoute $loadRoute -BoardCandidateFamily $boardCandidateFamily
        board_candidate_family = $boardCandidateFamily
        corpus_classification = $knownClassification.classification
        known_source_driver = $knownClassification.source_driver
        known_source_family = $knownClassification.source_family
        known_source_owner = $knownClassification.source_owner
        missing_parent_zip = $knownClassification.missing_parent_zip
        next_action = Get-EffectiveInventoryNextAction -DefaultAction $defaultNextAction -KnownClassification $knownClassification
        entry_count = $files.Count
        nested_archives = @()
        sample_entries = @($files | Sort-Object Name | Select-Object -First $MaxEntries -ExpandProperty Name)
    }
}

$roots = [System.Collections.Generic.List[string]]::new()
foreach ($rootValue in $Root) {
    foreach ($rootPath in @(Split-PathList $rootValue)) {
        $roots.Add([string]$rootPath)
    }
}
if ($roots.Count -eq 0 -and -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable("MNEMOS_IREM_CORPUS_ROOTS"))) {
    foreach ($rootPath in @(Split-PathList ([Environment]::GetEnvironmentVariable("MNEMOS_IREM_CORPUS_ROOTS")))) {
        $roots.Add([string]$rootPath)
    }
}
if ($roots.Count -eq 0 -and (Test-Path -LiteralPath "D:\emu\irem" -PathType Container)) {
    $roots.Add("D:\emu\irem")
}
if ($roots.Count -eq 0) {
    throw "No roots supplied. Pass -Root, set MNEMOS_IREM_CORPUS_ROOTS, or provide D:\emu\irem."
}

if ([string]::IsNullOrWhiteSpace($Out)) {
    $Out = Join-Path $repoRoot "build/scratch/irem-corpus/inventory.json"
} else {
    $Out = Resolve-RepoPath $Out
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null

$m14ManifestIds = Read-GameManifestIds "src/manifests/irem_m14/games"
$m15ManifestIds = Read-GameManifestIds "src/manifests/irem_m15/games"
$m27ManifestIds = Read-GameManifestIds "src/manifests/irem_m27/games"
$m47ManifestIds = Read-GameManifestIds "src/manifests/irem_m47/games"
$m52ManifestIds = Read-GameManifestIds "src/manifests/irem_m52/games"
$m58ManifestIds = Read-GameManifestIds "src/manifests/irem_m58/games"
$m62ManifestIds = Read-GameManifestIds "src/manifests/irem_m62/games"
$m63ManifestIds = Read-GameManifestIds "src/manifests/irem_m63/games"
$m72ManifestIds = Read-GameManifestIds "src/manifests/irem_m72/games"
$m75ManifestIds = Read-GameManifestIds "src/manifests/irem_m75/games"
$m81ManifestIds = Read-GameManifestIds "src/manifests/irem_m81/games"
$m82ManifestIds = Read-GameManifestIds "src/manifests/irem_m82/games"
$m84ManifestIds = Read-GameManifestIds "src/manifests/irem_m84/games"
$m90ManifestIds = Read-GameManifestIds "src/manifests/irem_m90/games"
$m92ManifestIds = Read-GameManifestIds "src/manifests/irem_m92/games"
$m107ManifestIds = Read-GameManifestIds "src/manifests/irem_m107/games"
$m14ManifestParents = Read-GameManifestParents "src/manifests/irem_m14/games"
$m15ManifestParents = Read-GameManifestParents "src/manifests/irem_m15/games"
$m27ManifestParents = Read-GameManifestParents "src/manifests/irem_m27/games"
$m47ManifestParents = Read-GameManifestParents "src/manifests/irem_m47/games"
$m52ManifestParents = Read-GameManifestParents "src/manifests/irem_m52/games"
$m58ManifestParents = Read-GameManifestParents "src/manifests/irem_m58/games"
$m62ManifestParents = Read-GameManifestParents "src/manifests/irem_m62/games"
$m63ManifestParents = Read-GameManifestParents "src/manifests/irem_m63/games"
$m72ManifestParents = Read-GameManifestParents "src/manifests/irem_m72/games"
$m75ManifestParents = Read-GameManifestParents "src/manifests/irem_m75/games"
$m81ManifestParents = Read-GameManifestParents "src/manifests/irem_m81/games"
$m82ManifestParents = Read-GameManifestParents "src/manifests/irem_m82/games"
$m84ManifestParents = Read-GameManifestParents "src/manifests/irem_m84/games"
$m90ManifestParents = Read-GameManifestParents "src/manifests/irem_m90/games"
$m92ManifestParents = Read-GameManifestParents "src/manifests/irem_m92/games"
$m107ManifestParents = Read-GameManifestParents "src/manifests/irem_m107/games"
$script:manifestParentBySet = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::OrdinalIgnoreCase)
Add-ManifestParents -ManifestIds $m14ManifestIds -ParentMap $m14ManifestParents
Add-ManifestParents -ManifestIds $m15ManifestIds -ParentMap $m15ManifestParents
Add-ManifestParents -ManifestIds $m27ManifestIds -ParentMap $m27ManifestParents
Add-ManifestParents -ManifestIds $m47ManifestIds -ParentMap $m47ManifestParents
Add-ManifestParents -ManifestIds $m52ManifestIds -ParentMap $m52ManifestParents
Add-ManifestParents -ManifestIds $m58ManifestIds -ParentMap $m58ManifestParents
Add-ManifestParents -ManifestIds $m62ManifestIds -ParentMap $m62ManifestParents
Add-ManifestParents -ManifestIds $m63ManifestIds -ParentMap $m63ManifestParents
Add-ManifestParents -ManifestIds $m72ManifestIds -ParentMap $m72ManifestParents
Add-ManifestParents -ManifestIds $m75ManifestIds -ParentMap $m75ManifestParents
Add-ManifestParents -ManifestIds $m81ManifestIds -ParentMap $m81ManifestParents
Add-ManifestParents -ManifestIds $m82ManifestIds -ParentMap $m82ManifestParents
Add-ManifestParents -ManifestIds $m84ManifestIds -ParentMap $m84ManifestParents
Add-ManifestParents -ManifestIds $m90ManifestIds -ParentMap $m90ManifestParents
Add-ManifestParents -ManifestIds $m92ManifestIds -ParentMap $m92ManifestParents
Add-ManifestParents -ManifestIds $m107ManifestIds -ParentMap $m107ManifestParents
$items = [System.Collections.Generic.List[object]]::new()
$resolvedRoots = [System.Collections.Generic.List[string]]::new()

foreach ($root in @($roots)) {
    $resolved = Resolve-RepoPath ([string]$root)
    if (-not (Test-Path -LiteralPath $resolved)) {
        Write-Warning "Irem corpus root not found: $root"
        continue
    }
    $rootPath = (Resolve-Path -LiteralPath $resolved).Path
    $resolvedRoots.Add($rootPath)

    if (Test-Path -LiteralPath $rootPath -PathType Leaf) {
        $file = Get-Item -LiteralPath $rootPath
        $bucket = Get-BucketForPath -RootPath (Split-Path -Parent $rootPath) -Path $rootPath
        $items.Add((New-ArchiveItem -File $file -Bucket $bucket -M14ManifestIds $m14ManifestIds -M15ManifestIds $m15ManifestIds -M27ManifestIds $m27ManifestIds -M47ManifestIds $m47ManifestIds -M52ManifestIds $m52ManifestIds -M58ManifestIds $m58ManifestIds -M62ManifestIds $m62ManifestIds -M63ManifestIds $m63ManifestIds -M72ManifestIds $m72ManifestIds -M75ManifestIds $m75ManifestIds -M81ManifestIds $m81ManifestIds -M82ManifestIds $m82ManifestIds -M84ManifestIds $m84ManifestIds -M90ManifestIds $m90ManifestIds -M92ManifestIds $m92ManifestIds -M107ManifestIds $m107ManifestIds))
        continue
    }

    $fileArgs = @{
        LiteralPath = $rootPath
        File = $true
        ErrorAction = "SilentlyContinue"
    }
    $dirArgs = @{
        LiteralPath = $rootPath
        Directory = $true
        ErrorAction = "SilentlyContinue"
    }
    if ($Recurse) {
        $fileArgs.Recurse = $true
        $dirArgs.Recurse = $true
    }

    foreach ($file in Get-ChildItem @fileArgs | Sort-Object FullName) {
        if ($file.Extension -notin @(".zip", ".7z")) {
            continue
        }
        $bucket = Get-BucketForPath -RootPath $rootPath -Path $file.FullName
        $items.Add((New-ArchiveItem -File $file -Bucket $bucket -M14ManifestIds $m14ManifestIds -M15ManifestIds $m15ManifestIds -M27ManifestIds $m27ManifestIds -M47ManifestIds $m47ManifestIds -M52ManifestIds $m52ManifestIds -M58ManifestIds $m58ManifestIds -M62ManifestIds $m62ManifestIds -M63ManifestIds $m63ManifestIds -M72ManifestIds $m72ManifestIds -M75ManifestIds $m75ManifestIds -M81ManifestIds $m81ManifestIds -M82ManifestIds $m82ManifestIds -M84ManifestIds $m84ManifestIds -M90ManifestIds $m90ManifestIds -M92ManifestIds $m92ManifestIds -M107ManifestIds $m107ManifestIds))
    }

    foreach ($directory in Get-ChildItem @dirArgs | Sort-Object FullName) {
        $bucket = Get-BucketForPath -RootPath $rootPath -Path $directory.FullName
        $item = New-DirectoryItem -Directory $directory -Bucket $bucket -M14ManifestIds $m14ManifestIds -M15ManifestIds $m15ManifestIds -M27ManifestIds $m27ManifestIds -M47ManifestIds $m47ManifestIds -M52ManifestIds $m52ManifestIds -M58ManifestIds $m58ManifestIds -M62ManifestIds $m62ManifestIds -M63ManifestIds $m63ManifestIds -M72ManifestIds $m72ManifestIds -M75ManifestIds $m75ManifestIds -M81ManifestIds $m81ManifestIds -M82ManifestIds $m82ManifestIds -M84ManifestIds $m84ManifestIds -M90ManifestIds $m90ManifestIds -M92ManifestIds $m92ManifestIds -M107ManifestIds $m107ManifestIds
        if ($null -ne $item) {
            $items.Add($item)
        }
    }
}

$bucketRows = @($items |
    Group-Object Bucket |
    Sort-Object Name |
    ForEach-Object {
        $groupItems = @($_.Group)
        [pscustomobject]@{
            bucket = $_.Name
            item_count = $groupItems.Count
            archive_count = @($groupItems | Where-Object { $_.kind -eq "archive" }).Count
            directory_count = @($groupItems | Where-Object { $_.kind -eq "directory" }).Count
            m14_manifest_matches = @($groupItems | Where-Object { $_.m14_manifest_match }).Count
            m15_manifest_matches = @($groupItems | Where-Object { $_.m15_manifest_match }).Count
            m27_manifest_matches = @($groupItems | Where-Object { $_.m27_manifest_match }).Count
            m47_manifest_matches = @($groupItems | Where-Object { $_.m47_manifest_match }).Count
            m52_manifest_matches = @($groupItems | Where-Object { $_.m52_manifest_match }).Count
            m58_manifest_matches = @($groupItems | Where-Object { $_.m58_manifest_match }).Count
            m62_manifest_matches = @($groupItems | Where-Object { $_.m62_manifest_match }).Count
            m63_manifest_matches = @($groupItems | Where-Object { $_.m63_manifest_match }).Count
            m72_manifest_matches = @($groupItems | Where-Object { $_.m72_manifest_match }).Count
            m75_manifest_matches = @($groupItems | Where-Object { $_.m75_manifest_match }).Count
            m81_manifest_matches = @($groupItems | Where-Object { $_.m81_manifest_match }).Count
            m82_manifest_matches = @($groupItems | Where-Object { $_.m82_manifest_match }).Count
            m84_manifest_matches = @($groupItems | Where-Object { $_.m84_manifest_match }).Count
            m90_manifest_matches = @($groupItems | Where-Object { $_.m90_manifest_match }).Count
            m92_manifest_matches = @($groupItems | Where-Object { $_.m92_manifest_match }).Count
            m107_manifest_matches = @($groupItems | Where-Object { $_.m107_manifest_match }).Count
            tracked_by_mnemos = @($groupItems | Where-Object { $_.tracked_by_mnemos }).Count
            loadable_by_mnemos = @($groupItems | Where-Object { $_.loadable_by_mnemos }).Count
            total_bytes = ($groupItems | Measure-Object size_bytes -Sum).Sum
            supported_by_mnemos = @($groupItems | Where-Object { $_.supported_by_mnemos }).Count
            unsupported_by_mnemos = @($groupItems | Where-Object { -not $_.supported_by_mnemos }).Count
            contract_only_tracked = @($groupItems | Where-Object { $_.load_readiness -eq "tracked_contract_only" }).Count
            metadata_only_tracked = @($groupItems | Where-Object { $_.load_readiness -eq "metadata_only_unpack_or_repack" }).Count
            clone_items = @($groupItems | Where-Object { $_.set_role -eq "clone_declares_parent" }).Count
            parent_or_standalone_items = @($groupItems | Where-Object { $_.set_role -eq "parent_or_standalone" }).Count
        }
    })

$unsupportedBuckets = @($bucketRows |
    Where-Object { $_.bucket -ne "M72" -and $_.tracked_by_mnemos -eq 0 } |
    Select-Object -ExpandProperty bucket)

$boardFamilyCandidates = @($items |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_.board_candidate_family) } |
    Group-Object board_candidate_family |
    Sort-Object Name |
    ForEach-Object {
        $groupItems = @($_.Group | Sort-Object set_id, path)
        $loadRouteRows = @($groupItems |
            Group-Object load_route |
            Sort-Object Name |
            ForEach-Object {
                [pscustomobject]@{
                    route = $_.Name
                    item_count = @($_.Group).Count
                }
            })
        [pscustomobject]@{
            board_family = $_.Name
            item_count = $groupItems.Count
            archive_count = @($groupItems | Where-Object { $_.kind -eq "archive" }).Count
            directory_count = @($groupItems | Where-Object { $_.kind -eq "directory" }).Count
            load_routes = @($loadRouteRows)
            candidate_set_ids = @($groupItems | Select-Object -ExpandProperty set_id -Unique)
            sample_items = @($groupItems |
                Select-Object -First $MaxEntries |
                ForEach-Object {
                    [pscustomobject]@{
                        set_id = $_.set_id
                        kind = $_.kind
                        load_route = $_.load_route
                        path = $_.path
                    }
                })
        }
    })

$trackedSetRows = @($items |
    Where-Object { $_.tracked_by_mnemos } |
    Group-Object tracked_family, set_id |
    Sort-Object Name |
    ForEach-Object {
        $groupItems = @($_.Group | Sort-Object path)
        $first = $groupItems[0]
        [pscustomobject]@{
            tracked_family = $first.tracked_family
            set_id = $first.set_id
            manifest_parent = $first.manifest_parent
            set_role = $first.set_role
            item_count = $groupItems.Count
            direct_player_loadable_count = @($groupItems | Where-Object { $_.load_readiness -eq "direct_player_loadable" }).Count
            contract_only_count = @($groupItems | Where-Object { $_.load_readiness -eq "tracked_contract_only" }).Count
            metadata_only_count = @($groupItems | Where-Object { $_.load_readiness -eq "metadata_only_unpack_or_repack" }).Count
            buckets = @($groupItems | Select-Object -ExpandProperty bucket -Unique)
            load_routes = @($groupItems | Select-Object -ExpandProperty load_route -Unique)
            archive_compositions = @($groupItems | Select-Object -ExpandProperty archive_composition -Unique)
            sample_items = @($groupItems |
                Select-Object -First $MaxEntries |
                ForEach-Object {
                    [pscustomobject]@{
                        bucket = $_.bucket
                        kind = $_.kind
                        load_route = $_.load_route
                        archive_composition = $_.archive_composition
                        load_readiness = $_.load_readiness
                        path = $_.path
                    }
                })
        }
    })

$knownCorpusRows = @($items |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_.corpus_classification) } |
    Group-Object corpus_classification, known_source_driver, known_source_family, known_source_owner, missing_parent_zip |
    Sort-Object Name |
    ForEach-Object {
        $groupItems = @($_.Group | Sort-Object set_id, path)
        $first = $groupItems[0]
        [pscustomobject]@{
            corpus_classification = $first.corpus_classification
            source_driver = $first.known_source_driver
            source_family = $first.known_source_family
            source_owner = $first.known_source_owner
            missing_parent_zip = $first.missing_parent_zip
            item_count = $groupItems.Count
            set_ids = @($groupItems | Select-Object -ExpandProperty set_id -Unique)
            next_actions = @($groupItems | Select-Object -ExpandProperty next_action -Unique)
            sample_items = @($groupItems |
                Select-Object -First $MaxEntries |
                ForEach-Object {
                    [pscustomobject]@{
                        set_id = $_.set_id
                        kind = $_.kind
                        load_route = $_.load_route
                        path = $_.path
                    }
                })
        }
    })

$report = [pscustomobject]@{
    generated_at = (Get-Date).ToString("o")
    roots = @($resolvedRoots)
    recurse = [bool]$Recurse
    known_buckets = @("M10", "M14", "M15", "M27", "M47", "M52", "M57", "M58", "M62", "M63", "M72", "M75", "M78", "M81", "M82", "M84", "M85", "M90", "M92", "M102", "M107", "i8751", "travrusa", "for-delete", "misc", "non-irem", "root")
    m14_manifest_count = $m14ManifestIds.Count
    m15_manifest_count = $m15ManifestIds.Count
    m27_manifest_count = $m27ManifestIds.Count
    m47_manifest_count = $m47ManifestIds.Count
    m52_manifest_count = $m52ManifestIds.Count
    m58_manifest_count = $m58ManifestIds.Count
    m62_manifest_count = $m62ManifestIds.Count
    m63_manifest_count = $m63ManifestIds.Count
    m72_manifest_count = $m72ManifestIds.Count
    m75_manifest_count = $m75ManifestIds.Count
    m81_manifest_count = $m81ManifestIds.Count
    m82_manifest_count = $m82ManifestIds.Count
    m84_manifest_count = $m84ManifestIds.Count
    m90_manifest_count = $m90ManifestIds.Count
    m92_manifest_count = $m92ManifestIds.Count
    m107_manifest_count = $m107ManifestIds.Count
    summary = [pscustomobject]@{
        item_count = $items.Count
        archive_count = @($items | Where-Object { $_.kind -eq "archive" }).Count
        directory_count = @($items | Where-Object { $_.kind -eq "directory" }).Count
        m14_manifest_matches = @($items | Where-Object { $_.m14_manifest_match }).Count
        m15_manifest_matches = @($items | Where-Object { $_.m15_manifest_match }).Count
        m27_manifest_matches = @($items | Where-Object { $_.m27_manifest_match }).Count
        m47_manifest_matches = @($items | Where-Object { $_.m47_manifest_match }).Count
        m52_manifest_matches = @($items | Where-Object { $_.m52_manifest_match }).Count
        m58_manifest_matches = @($items | Where-Object { $_.m58_manifest_match }).Count
        m62_manifest_matches = @($items | Where-Object { $_.m62_manifest_match }).Count
        m63_manifest_matches = @($items | Where-Object { $_.m63_manifest_match }).Count
        m72_manifest_matches = @($items | Where-Object { $_.m72_manifest_match }).Count
        m75_manifest_matches = @($items | Where-Object { $_.m75_manifest_match }).Count
        m81_manifest_matches = @($items | Where-Object { $_.m81_manifest_match }).Count
        m82_manifest_matches = @($items | Where-Object { $_.m82_manifest_match }).Count
        m84_manifest_matches = @($items | Where-Object { $_.m84_manifest_match }).Count
        m90_manifest_matches = @($items | Where-Object { $_.m90_manifest_match }).Count
        m92_manifest_matches = @($items | Where-Object { $_.m92_manifest_match }).Count
        m107_manifest_matches = @($items | Where-Object { $_.m107_manifest_match }).Count
        tracked_item_count = @($items | Where-Object { $_.tracked_by_mnemos }).Count
        loadable_item_count = @($items | Where-Object { $_.loadable_by_mnemos }).Count
        supported_item_count = @($items | Where-Object { $_.supported_by_mnemos }).Count
        contract_only_tracked_item_count = @($items | Where-Object { $_.load_readiness -eq "tracked_contract_only" }).Count
        metadata_only_tracked_item_count = @($items | Where-Object { $_.load_readiness -eq "metadata_only_unpack_or_repack" }).Count
        tracked_clone_item_count = @($items | Where-Object { $_.set_role -eq "clone_declares_parent" }).Count
        tracked_parent_or_standalone_item_count = @($items | Where-Object { $_.set_role -eq "parent_or_standalone" }).Count
        known_corpus_classification_count = $knownCorpusRows.Count
        known_untracked_item_count = @($items | Where-Object { -not $_.tracked_by_mnemos -and -not [string]::IsNullOrWhiteSpace($_.corpus_classification) }).Count
        irem_split_clone_parent_present_item_count = @($items | Where-Object { $_.corpus_classification -eq "irem_split_clone_parent_present" }).Count
        non_irem_reference_item_count = @($items | Where-Object { $_.corpus_classification -eq "non_irem_reference" }).Count
        unsupported_item_count = @($items | Where-Object { -not $_.supported_by_mnemos }).Count
        unsupported_bucket_count = $unsupportedBuckets.Count
        unsupported_buckets = @($unsupportedBuckets)
    }
    buckets = @($bucketRows)
    board_family_candidates = @($boardFamilyCandidates)
    known_corpus_items = @($knownCorpusRows)
    tracked_sets = @($trackedSetRows)
    items = @($items)
}

$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding UTF8

Write-Host ("Irem corpus inventory: {0} item(s), {1} bucket(s); report: {2}" -f $items.Count, $bucketRows.Count, $Out)
foreach ($bucket in $bucketRows) {
    Write-Host ("  [{0}] items={1} archives={2} dirs={3} m14_matches={4} m15_matches={5} m27_matches={6} m47_matches={7} m52_matches={8} m58_matches={9} m62_matches={10} m63_matches={11} m72_matches={12} m75_matches={13} m81_matches={14} m82_matches={15} m84_matches={16} m90_matches={17} m92_matches={18} m107_matches={19} tracked={20} loadable={21} supported={22} contract_only={23} metadata_only={24}" -f $bucket.bucket, $bucket.item_count, $bucket.archive_count, $bucket.directory_count, $bucket.m14_manifest_matches, $bucket.m15_manifest_matches, $bucket.m27_manifest_matches, $bucket.m47_manifest_matches, $bucket.m52_manifest_matches, $bucket.m58_manifest_matches, $bucket.m62_manifest_matches, $bucket.m63_manifest_matches, $bucket.m72_manifest_matches, $bucket.m75_manifest_matches, $bucket.m81_manifest_matches, $bucket.m82_manifest_matches, $bucket.m84_manifest_matches, $bucket.m90_manifest_matches, $bucket.m92_manifest_matches, $bucket.m107_manifest_matches, $bucket.tracked_by_mnemos, $bucket.loadable_by_mnemos, $bucket.supported_by_mnemos, $bucket.contract_only_tracked, $bucket.metadata_only_tracked)
}
if ($boardFamilyCandidates.Count -gt 0) {
    Write-Host "  board-family candidates:"
    foreach ($candidate in $boardFamilyCandidates) {
        Write-Host ("    [{0}] items={1} sets={2}" -f $candidate.board_family, $candidate.item_count, ($candidate.candidate_set_ids -join ","))
    }
}
if ($knownCorpusRows.Count -gt 0) {
    Write-Host "  known corpus classifications:"
    foreach ($known in $knownCorpusRows) {
        $missing = if ([string]::IsNullOrWhiteSpace($known.missing_parent_zip)) { "-" } else { $known.missing_parent_zip }
        Write-Host ("    [{0}] source={1} items={2} sets={3} missing_parent={4} next={5}" -f $known.corpus_classification, $known.source_driver, $known.item_count, ($known.set_ids -join ","), $missing, ($known.next_actions -join ","))
    }
}
$metadataOnlyTracked = @($trackedSetRows | Where-Object { $_.metadata_only_count -gt 0 })
if ($metadataOnlyTracked.Count -gt 0) {
    Write-Host "  tracked metadata-only archives:"
    foreach ($set in $metadataOnlyTracked) {
        Write-Host ("    [{0}] {1} parent={2} direct_loadable={3} contract_only={4} metadata_only={5}" -f $set.tracked_family, $set.set_id, $(if ([string]::IsNullOrWhiteSpace($set.manifest_parent)) { "-" } else { $set.manifest_parent }), $set.direct_player_loadable_count, $set.contract_only_count, $set.metadata_only_count)
    }
}
