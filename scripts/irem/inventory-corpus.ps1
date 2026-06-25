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
        "m15" { return "M15" }
        "m72" { return "M72" }
        "m81" { return "M81" }
        "m82" { return "M82" }
        "m84" { return "M84" }
        "m107" { return "M107" }
        "i8751" { return "i8751" }
        default { return "" }
    }
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
    if ($NestedArchives.Count -eq 1) {
        return [System.IO.Path]::GetFileNameWithoutExtension($NestedArchives[0])
    }
    return [System.IO.Path]::GetFileNameWithoutExtension($Path)
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

function Get-BoardCandidateFamily {
    param(
        [Parameter(Mandatory = $true)][string]$Bucket,
        [Parameter(Mandatory = $true)][bool]$TrackedByMnemos
    )
    if ($TrackedByMnemos) {
        return ""
    }
    if ($Bucket -in @("M15", "M72", "M81", "M82", "M84", "M107", "i8751")) {
        return $Bucket
    }
    return ""
}

function Get-InventoryNextAction {
    param(
        [Parameter(Mandatory = $true)][bool]$TrackedByMnemos,
        [Parameter(Mandatory = $true)][bool]$LoadableByMnemos,
        [Parameter(Mandatory = $true)][string]$LoadRoute,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$BoardCandidateFamily
    )
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

function New-ArchiveItem {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileInfo]$File,
        [Parameter(Mandatory = $true)][string]$Bucket,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M15ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M72ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M81ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M82ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M84ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M107ManifestIds
    )

    $entries = @(Get-ArchiveEntries -Path $File.FullName)
    $nestedArchives = @($entries |
        Where-Object { $_ -match '\.(zip|7z)$' } |
        ForEach-Object { [System.IO.Path]::GetFileName($_) } |
        Sort-Object -Unique)
    $setId = Get-SetIdFromPath -Path $File.FullName -NestedArchives $nestedArchives
    $m15Match = $M15ManifestIds.Contains($setId)
    $m72Match = $M72ManifestIds.Contains($setId)
    $m81Match = $M81ManifestIds.Contains($setId)
    $m82Match = $M82ManifestIds.Contains($setId)
    $m84Match = $M84ManifestIds.Contains($setId)
    $m107Match = $M107ManifestIds.Contains($setId)
    $trackedMatch = $m15Match -or $m72Match -or $m81Match -or $m82Match -or $m84Match -or $m107Match
    $loadRoute = Get-LoadRouteForItem -Kind "archive" -Extension $File.Extension -NestedArchives $nestedArchives
    $loadableByMnemos = $trackedMatch -and (Test-MnemosLoadableRoute -LoadRoute $loadRoute)
    $boardCandidateFamily = Get-BoardCandidateFamily -Bucket $Bucket -TrackedByMnemos $trackedMatch

    return [pscustomobject]@{
        kind = "archive"
        bucket = $Bucket
        path = $File.FullName
        name = $File.Name
        extension = $File.Extension.ToLowerInvariant()
        size_bytes = $File.Length
        set_id = $setId
        m15_manifest_match = $m15Match
        m72_manifest_match = $m72Match
        m81_manifest_match = $m81Match
        m82_manifest_match = $m82Match
        m84_manifest_match = $m84Match
        m107_manifest_match = $m107Match
        tracked_by_mnemos = $trackedMatch
        load_route = $loadRoute
        loadable_by_mnemos = $loadableByMnemos
        supported_by_mnemos = $loadableByMnemos
        board_candidate_family = $boardCandidateFamily
        next_action = Get-InventoryNextAction -TrackedByMnemos $trackedMatch -LoadableByMnemos $loadableByMnemos -LoadRoute $loadRoute -BoardCandidateFamily $boardCandidateFamily
        entry_count = $entries.Count
        nested_archives = @($nestedArchives)
        sample_entries = @($entries | Select-Object -First $MaxEntries)
    }
}

function New-DirectoryItem {
    param(
        [Parameter(Mandatory = $true)][System.IO.DirectoryInfo]$Directory,
        [Parameter(Mandatory = $true)][string]$Bucket,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M15ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M72ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M81ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M82ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M84ManifestIds,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$M107ManifestIds
    )

    $files = @(Get-ChildItem -LiteralPath $Directory.FullName -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -notin @(".zip", ".7z") })
    if ($files.Count -eq 0) {
        return $null
    }

    $setId = $Directory.Name
    $m15Match = $M15ManifestIds.Contains($setId)
    $m72Match = $M72ManifestIds.Contains($setId)
    $m81Match = $M81ManifestIds.Contains($setId)
    $m82Match = $M82ManifestIds.Contains($setId)
    $m84Match = $M84ManifestIds.Contains($setId)
    $m107Match = $M107ManifestIds.Contains($setId)
    $trackedMatch = $m15Match -or $m72Match -or $m81Match -or $m82Match -or $m84Match -or $m107Match
    $loadRoute = Get-LoadRouteForItem -Kind "directory" -Extension "" -NestedArchives @()
    $loadableByMnemos = $trackedMatch -and (Test-MnemosLoadableRoute -LoadRoute $loadRoute)
    $boardCandidateFamily = Get-BoardCandidateFamily -Bucket $Bucket -TrackedByMnemos $trackedMatch
    return [pscustomobject]@{
        kind = "directory"
        bucket = $Bucket
        path = $Directory.FullName
        name = $Directory.Name
        extension = ""
        size_bytes = ($files | Measure-Object Length -Sum).Sum
        set_id = $setId
        m15_manifest_match = $m15Match
        m72_manifest_match = $m72Match
        m81_manifest_match = $m81Match
        m82_manifest_match = $m82Match
        m84_manifest_match = $m84Match
        m107_manifest_match = $m107Match
        tracked_by_mnemos = $trackedMatch
        load_route = $loadRoute
        loadable_by_mnemos = $loadableByMnemos
        supported_by_mnemos = $loadableByMnemos
        board_candidate_family = $boardCandidateFamily
        next_action = Get-InventoryNextAction -TrackedByMnemos $trackedMatch -LoadableByMnemos $loadableByMnemos -LoadRoute $loadRoute -BoardCandidateFamily $boardCandidateFamily
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

$m15ManifestIds = Read-GameManifestIds "src/manifests/irem_m15/games"
$m72ManifestIds = Read-GameManifestIds "src/manifests/irem_m72/games"
$m81ManifestIds = Read-GameManifestIds "src/manifests/irem_m81/games"
$m82ManifestIds = Read-GameManifestIds "src/manifests/irem_m82/games"
$m84ManifestIds = Read-GameManifestIds "src/manifests/irem_m84/games"
$m107ManifestIds = Read-GameManifestIds "src/manifests/irem_m107/games"
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
        $items.Add((New-ArchiveItem -File $file -Bucket $bucket -M15ManifestIds $m15ManifestIds -M72ManifestIds $m72ManifestIds -M81ManifestIds $m81ManifestIds -M82ManifestIds $m82ManifestIds -M84ManifestIds $m84ManifestIds -M107ManifestIds $m107ManifestIds))
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
        $items.Add((New-ArchiveItem -File $file -Bucket $bucket -M15ManifestIds $m15ManifestIds -M72ManifestIds $m72ManifestIds -M81ManifestIds $m81ManifestIds -M82ManifestIds $m82ManifestIds -M84ManifestIds $m84ManifestIds -M107ManifestIds $m107ManifestIds))
    }

    foreach ($directory in Get-ChildItem @dirArgs | Sort-Object FullName) {
        $bucket = Get-BucketForPath -RootPath $rootPath -Path $directory.FullName
        $item = New-DirectoryItem -Directory $directory -Bucket $bucket -M15ManifestIds $m15ManifestIds -M72ManifestIds $m72ManifestIds -M81ManifestIds $m81ManifestIds -M82ManifestIds $m82ManifestIds -M84ManifestIds $m84ManifestIds -M107ManifestIds $m107ManifestIds
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
            m15_manifest_matches = @($groupItems | Where-Object { $_.m15_manifest_match }).Count
            m72_manifest_matches = @($groupItems | Where-Object { $_.m72_manifest_match }).Count
            m81_manifest_matches = @($groupItems | Where-Object { $_.m81_manifest_match }).Count
            m82_manifest_matches = @($groupItems | Where-Object { $_.m82_manifest_match }).Count
            m84_manifest_matches = @($groupItems | Where-Object { $_.m84_manifest_match }).Count
            m107_manifest_matches = @($groupItems | Where-Object { $_.m107_manifest_match }).Count
            tracked_by_mnemos = @($groupItems | Where-Object { $_.tracked_by_mnemos }).Count
            loadable_by_mnemos = @($groupItems | Where-Object { $_.loadable_by_mnemos }).Count
            total_bytes = ($groupItems | Measure-Object size_bytes -Sum).Sum
            supported_by_mnemos = @($groupItems | Where-Object { $_.supported_by_mnemos }).Count
            unsupported_by_mnemos = @($groupItems | Where-Object { -not $_.supported_by_mnemos }).Count
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

$report = [pscustomobject]@{
    generated_at = (Get-Date).ToString("o")
    roots = @($resolvedRoots)
    recurse = [bool]$Recurse
    known_buckets = @("M15", "M72", "M81", "M82", "M84", "M107", "i8751", "root")
    m15_manifest_count = $m15ManifestIds.Count
    m72_manifest_count = $m72ManifestIds.Count
    m81_manifest_count = $m81ManifestIds.Count
    m82_manifest_count = $m82ManifestIds.Count
    m84_manifest_count = $m84ManifestIds.Count
    m107_manifest_count = $m107ManifestIds.Count
    summary = [pscustomobject]@{
        item_count = $items.Count
        archive_count = @($items | Where-Object { $_.kind -eq "archive" }).Count
        directory_count = @($items | Where-Object { $_.kind -eq "directory" }).Count
        m15_manifest_matches = @($items | Where-Object { $_.m15_manifest_match }).Count
        m72_manifest_matches = @($items | Where-Object { $_.m72_manifest_match }).Count
        m81_manifest_matches = @($items | Where-Object { $_.m81_manifest_match }).Count
        m82_manifest_matches = @($items | Where-Object { $_.m82_manifest_match }).Count
        m84_manifest_matches = @($items | Where-Object { $_.m84_manifest_match }).Count
        m107_manifest_matches = @($items | Where-Object { $_.m107_manifest_match }).Count
        tracked_item_count = @($items | Where-Object { $_.tracked_by_mnemos }).Count
        loadable_item_count = @($items | Where-Object { $_.loadable_by_mnemos }).Count
        supported_item_count = @($items | Where-Object { $_.supported_by_mnemos }).Count
        unsupported_item_count = @($items | Where-Object { -not $_.supported_by_mnemos }).Count
        unsupported_bucket_count = $unsupportedBuckets.Count
        unsupported_buckets = @($unsupportedBuckets)
    }
    buckets = @($bucketRows)
    board_family_candidates = @($boardFamilyCandidates)
    items = @($items)
}

$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding UTF8

Write-Host ("Irem corpus inventory: {0} item(s), {1} bucket(s); report: {2}" -f $items.Count, $bucketRows.Count, $Out)
foreach ($bucket in $bucketRows) {
    Write-Host ("  [{0}] items={1} archives={2} dirs={3} m15_matches={4} m72_matches={5} m81_matches={6} m82_matches={7} m84_matches={8} m107_matches={9} tracked={10} loadable={11} supported={12}" -f $bucket.bucket, $bucket.item_count, $bucket.archive_count, $bucket.directory_count, $bucket.m15_manifest_matches, $bucket.m72_manifest_matches, $bucket.m81_manifest_matches, $bucket.m82_manifest_matches, $bucket.m84_manifest_matches, $bucket.m107_manifest_matches, $bucket.tracked_by_mnemos, $bucket.loadable_by_mnemos, $bucket.supported_by_mnemos)
}
if ($boardFamilyCandidates.Count -gt 0) {
    Write-Host "  board-family candidates:"
    foreach ($candidate in $boardFamilyCandidates) {
        Write-Host ("    [{0}] items={1} sets={2}" -f $candidate.board_family, $candidate.item_count, ($candidate.candidate_set_ids -join ","))
    }
}
