#!/usr/bin/env pwsh
# Inventory a local Taito arcade corpus against the Mnemos-supported board set.
#
# This is not an emulator oracle. It records which local packages are runnable by
# checked-in Mnemos manifests today and which packages still need distinct board
# implementations or media handling before the broader Taito goal can be closed.

param(
    [string]$RomDir = "",
    [string]$OutDir = "",
    [switch]$Recurse,
    [switch]$RequireAllSupported
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Get-TaitoF2ManifestMap {
    $gamesDir = Join-Path $repoRoot "src/manifests/taito_f2/games"
    $map = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    if (Test-Path -LiteralPath $gamesDir -PathType Container) {
        foreach ($toml in Get-ChildItem -LiteralPath $gamesDir -Filter "*.toml" -File) {
            $setId = [System.IO.Path]::GetFileNameWithoutExtension($toml.Name)
            $map[$setId] = $toml.FullName
        }
    }
    return $map
}

function Resolve-TaitoF2ManifestId {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$ManifestMap
    )
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    if ($ManifestMap.ContainsKey($stem)) {
        return $stem
    }
    $underscore = $stem.IndexOf("_", [System.StringComparison]::Ordinal)
    if ($underscore -gt 0) {
        $base = $stem.Substring(0, $underscore)
        if ($ManifestMap.ContainsKey($base)) {
            return $base
        }
    }
    return $null
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $rootUri = [System.Uri]::new((Resolve-Path -LiteralPath $Root).Path.TrimEnd('\') + '\')
    $pathUri = [System.Uri]::new((Resolve-Path -LiteralPath $Path).Path)
    return [System.Uri]::UnescapeDataString(
        $rootUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

function Get-ZipEntryNames {
    param([Parameter(Mandatory = $true)][string]$Path)
    $archive = $null
    try {
        $archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
        $names = [System.Collections.Generic.List[string]]::new()
        foreach ($entry in $archive.Entries) {
            if (-not [string]::IsNullOrWhiteSpace($entry.Name)) {
                $names.Add($entry.FullName)
            }
        }
        return ,$names
    } finally {
        if ($null -ne $archive) {
            $archive.Dispose()
        }
    }
}

function New-InventoryRow {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileInfo]$File,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][bool]$Supported,
        [string]$SupportedSystem = "",
        [string]$SetId = "",
        [Parameter(Mandatory = $true)][string]$PackageKind,
        [Parameter(Mandatory = $true)][string]$Reason,
        [string]$BoardHint = "",
        [string]$Workstream = "",
        [string[]]$RequiredCapabilities = @(),
        [string[]]$ZipEntries = @(),
        [string[]]$ChdEntries = @(),
        [string[]]$NestedZipEntries = @()
    )
    return [pscustomobject]@{
        name = $File.Name
        relative_path = $RelativePath
        path = $File.FullName
        extension = $File.Extension.ToLowerInvariant()
        size_bytes = $File.Length
        supported_by_mnemos = $Supported
        supported_system = $SupportedSystem
        set_id = $SetId
        package_kind = $PackageKind
        reason = $Reason
        board_hint = $BoardHint
        workstream = $Workstream
        required_capabilities = @($RequiredCapabilities)
        zip_entry_sample = @($ZipEntries | Select-Object -First 16)
        chd_entries = @($ChdEntries)
        nested_zip_entries = @($NestedZipEntries)
    }
}

function New-CoverageHint {
    param(
        [Parameter(Mandatory = $true)][string]$PackageKind,
        [Parameter(Mandatory = $true)][string]$BoardHint,
        [Parameter(Mandatory = $true)][string]$Workstream,
        [Parameter(Mandatory = $true)][string]$Reason,
        [string[]]$RequiredCapabilities = @()
    )
    return [pscustomobject]@{
        PackageKind = $PackageKind
        BoardHint = $BoardHint
        Workstream = $Workstream
        Reason = $Reason
        RequiredCapabilities = @($RequiredCapabilities)
    }
}

function Test-AnyToken {
    param(
        [Parameter(Mandatory = $true)][string[]]$Tokens,
        [Parameter(Mandatory = $true)][string[]]$Candidates
    )
    foreach ($candidate in $Candidates) {
        if ($Tokens -contains $candidate) {
            return $true
        }
    }
    return $false
}

function Get-CoverageHint {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [string]$SetId = "",
        [string[]]$ZipEntries = @(),
        [string[]]$ChdEntries = @(),
        [string[]]$NestedZipEntries = @()
    )

    $tokenList = [System.Collections.Generic.List[string]]::new()
    foreach ($value in @($RelativePath, $SetId) + $ZipEntries + $ChdEntries + $NestedZipEntries) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        $filename = [System.IO.Path]::GetFileNameWithoutExtension($value)
        $tokenList.Add($filename.ToLowerInvariant())
        $tokenList.Add($value.ToLowerInvariant())
    }
    $tokens = @($tokenList | Sort-Object -Unique)

    if (Test-AnyToken -Tokens $tokens -Candidates @("gunforce", "gunforcej", "gunforceu",
            "gunforc2", "geostorm")) {
        return New-CoverageHint -PackageKind "non_taito_irem_m92_wrapper" `
            -BoardHint "Irem M92 (non-Taito package)" `
            -Workstream "not_taito_arcade" `
            -Reason "Nested set id is an Irem M92 title, so this local F2-folder package is not Taito arcade coverage." `
            -RequiredCapabilities @("future_irem_m92_manifest", "v33_or_v30_family_board_work",
                "irem_m92_video_and_sprite_path")
    }

    if (Test-AnyToken -Tokens $tokens -Candidates @("chaoshea", "chaosheat", "chaosheatj",
            "gobyrc", "rcdego", "raycris", "raycrisj", "sianniv", "spuzbobl",
            "spuzbobj")) {
        return New-CoverageHint -PackageKind "chd_taito_gnet_package" `
            -BoardHint "Taito G-NET / Sony ZN-2 class" `
            -Workstream "taito_gnet" `
            -Reason "CHD package names map to the Taito G-NET family; Mnemos has an R3000A CPU bootstrap, ZIP-wrapped CHD flash-card decode, a boot-ROM/main-RAM/scratchpad board shell, FC-board flash-bank/PCMCIA apertures, a minimal RF5C296-style IO proxy, first BIOS-facing memory/cache control latches, a GPU register/VRAM latch shell, COP2/GTE register-transfer and command-latch shell, IRQ/root-timer latches with first-pass target/overflow IRQ delivery, limited GPU command and OTC DMA execution, and a board-smoke player adapter gated by MNEMOS_TAITO_GNET_BIOS, but these packages do not carry a BIOS ROM and there is no locked-card command/security protocol, GPU renderer/SPU/real GTE command math, full DMA timing/exact timer sync and clock-source/JVS path, or playable video/audio/input presentation yet." `
            -RequiredCapabilities @("gnet_bios_rom", "sony_zn_gpu_spu_real_gte",
                "gnet_locked_card_protocol", "gnet_dma_timing_exact_timer_sync_jvs_io",
                "gnet_playable_video_audio_input")
    }

    if (Test-AnyToken -Tokens $tokens -Candidates @("dendego3", "ddg3")) {
        return New-CoverageHint -PackageKind "chd_taito_type_zero_package" `
            -BoardHint "Taito Type Zero" `
            -Workstream "taito_type_zero" `
            -Reason "CHD package names map to Taito Type Zero; Mnemos has no PowerPC/Type-Zero board implementation." `
            -RequiredCapabilities @("powerpc_603e_cpu", "taito_type_zero_3d_video",
                "tlcs900_mcu", "ata_hdd_chd_media")
    }

    if (Test-AnyToken -Tokens $tokens -Candidates @("chasehq2", "chase_hq_2_v2.0.6.jp")) {
        return New-CoverageHint -PackageKind "chd_taito_type_x2_package" `
            -BoardHint "Taito Type X2" `
            -Workstream "taito_type_x" `
            -Reason "CHD package names map to the PC-based Taito Type X/X2 family; Mnemos has no x86/Windows/JVS arcade-PC platform." `
            -RequiredCapabilities @("x86_pc_platform", "windows_xp_embedded_runtime",
                "jvs_io", "hdd_chd_or_extracted_filesystem_media")
    }

    if (Test-AnyToken -Tokens $tokens -Candidates @("zoidiexp", "zoidsinf-ex-plus-ver2-10")) {
        return New-CoverageHint -PackageKind "chd_namco_system246_taito_title" `
            -BoardHint "Namco System 246 (Tomy/Taito title)" `
            -Workstream "not_taito_hardware" `
            -Reason "This is a Taito-published/local Taito-corpus title on Namco System 246 hardware, not a Taito board implementation target." `
            -RequiredCapabilities @("ps2_emotion_engine_class_cpu", "namco_system246",
                "dvd_chd_media")
    }

    $normalizedRelativePath = $RelativePath.Replace('/', '\')
    if ($normalizedRelativePath.StartsWith("Type X\", [System.StringComparison]::OrdinalIgnoreCase)) {
        return New-CoverageHint -PackageKind "taito_type_x_rar_package" `
            -BoardHint "Taito Type X / Type X2 PC family" `
            -Workstream "taito_type_x" `
            -Reason "Type X packages are PC-based arcade software archives; Mnemos has no x86/Windows/JVS arcade-PC platform." `
            -RequiredCapabilities @("rar_or_extracted_directory_media", "x86_pc_platform",
                "windows_xp_embedded_runtime", "jvs_io", "per_game_dongle_or_container_model")
    }

    if ($ChdEntries.Count -gt 0) {
        return New-CoverageHint -PackageKind "chd_taito_unknown_package" `
            -BoardHint "Unknown Taito CHD-media board" `
            -Workstream "taito_chd_triage" `
            -Reason "Archive carries CHD media and has no checked-in Taito F2 set manifest; it needs board-specific CHD/media support." `
            -RequiredCapabilities @("board_identification", "chd_media")
    }

    return New-CoverageHint -PackageKind "unmatched_zip" `
        -BoardHint "Unknown" `
        -Workstream "taito_triage" `
        -Reason "Zip basename and contents do not resolve to a checked-in Mnemos Taito F2 manifest."
}

function Get-ZipInventoryRow {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileInfo]$File,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)]$ManifestMap
    )

    $directSetId = Resolve-TaitoF2ManifestId -Path $File.FullName -ManifestMap $ManifestMap
    $entries = Get-ZipEntryNames -Path $File.FullName
    $nestedZipEntries = @($entries | Where-Object {
            $_.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)
        })
    $chdEntries = @($entries | Where-Object {
            $_.EndsWith(".chd", [System.StringComparison]::OrdinalIgnoreCase)
        })

    if ($null -ne $directSetId) {
        return New-InventoryRow -File $File -RelativePath $RelativePath -Supported $true `
            -SupportedSystem "taito_f2" -SetId $directSetId -PackageKind "taito_f2_set_zip" `
            -Reason "Archive basename resolves to a checked-in Taito F2 manifest." `
            -BoardHint "Taito F2" -Workstream "supported_taito_f2" `
            -ZipEntries $entries -ChdEntries $chdEntries -NestedZipEntries $nestedZipEntries
    }

    foreach ($entry in $nestedZipEntries) {
        $nestedSetId = Resolve-TaitoF2ManifestId -Path $entry -ManifestMap $ManifestMap
        if ($null -ne $nestedSetId) {
            return New-InventoryRow -File $File -RelativePath $RelativePath -Supported $true `
                -SupportedSystem "taito_f2" -SetId $nestedSetId `
                -PackageKind "taito_f2_title_wrapper_zip" `
                -Reason "One nested zip resolves to a checked-in Taito F2 manifest." `
                -BoardHint "Taito F2" -Workstream "supported_taito_f2" `
                -ZipEntries $entries -ChdEntries $chdEntries -NestedZipEntries $nestedZipEntries
        }
    }

    $hint = Get-CoverageHint -RelativePath $RelativePath -ZipEntries $entries `
        -ChdEntries $chdEntries -NestedZipEntries $nestedZipEntries

    return New-InventoryRow -File $File -RelativePath $RelativePath -Supported $false `
        -PackageKind $hint.PackageKind -Reason $hint.Reason -BoardHint $hint.BoardHint `
        -Workstream $hint.Workstream -RequiredCapabilities $hint.RequiredCapabilities `
        -ZipEntries $entries -ChdEntries $chdEntries -NestedZipEntries $nestedZipEntries
}

if ([string]::IsNullOrWhiteSpace($RomDir)) {
    $RomDir = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_SET_DIR")
}
if ([string]::IsNullOrWhiteSpace($RomDir)) {
    $RomDir = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_SET_DIR")
}
if ([string]::IsNullOrWhiteSpace($RomDir)) {
    Write-Host "No Taito corpus configured; set MNEMOS_TAITO_SET_DIR or pass -RomDir." `
        -ForegroundColor DarkGray
    exit 0
}

$resolvedDir = Resolve-RepoPath $RomDir
if (-not (Test-Path -LiteralPath $resolvedDir -PathType Container)) {
    throw "Taito corpus directory not found: $RomDir"
}

$manifestMap = Get-TaitoF2ManifestMap
$childArgs = @{
    LiteralPath = $resolvedDir
    File = $true
    ErrorAction = "SilentlyContinue"
}
if ($Recurse) {
    $childArgs.Recurse = $true
}

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($file in Get-ChildItem @childArgs | Sort-Object FullName) {
    $relative = Get-RelativePath -Root $resolvedDir -Path $file.FullName
    switch ($file.Extension.ToLowerInvariant()) {
        ".zip" {
            try {
                $rows.Add((Get-ZipInventoryRow -File $file -RelativePath $relative `
                    -ManifestMap $manifestMap))
            } catch {
                $rows.Add((New-InventoryRow -File $file -RelativePath $relative `
                    -Supported $false -PackageKind "unreadable_zip" `
                    -Reason ("Zip archive could not be inspected: " + $_.Exception.Message)))
            }
            break
        }
        ".rar" {
            $hint = Get-CoverageHint -RelativePath $relative
            $rows.Add((New-InventoryRow -File $file -RelativePath $relative -Supported $false `
                -PackageKind $hint.PackageKind -Reason $hint.Reason `
                -BoardHint $hint.BoardHint -Workstream $hint.Workstream `
                -RequiredCapabilities $hint.RequiredCapabilities))
            break
        }
        default {
            $rows.Add((New-InventoryRow -File $file -RelativePath $relative -Supported $false `
                -PackageKind "unsupported_file" `
                -Reason "File type is not consumed by a Mnemos Taito arcade adapter."))
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = Join-Path $repoRoot "build/scratch/taito-corpus-inventory/$stamp"
} else {
    $OutDir = Resolve-RepoPath $OutDir
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$supportedRows = @($rows | Where-Object { $_.supported_by_mnemos })
$unsupportedRows = @($rows | Where-Object { -not $_.supported_by_mnemos })
$summary = [pscustomobject]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    rom_root = (Resolve-Path -LiteralPath $resolvedDir).Path
    recurse = [bool]$Recurse
    total_packages = $rows.Count
    supported_packages = $supportedRows.Count
    unsupported_packages = $unsupportedRows.Count
    supported_sets = @($supportedRows | ForEach-Object { $_.set_id } | Sort-Object -Unique)
    by_package_kind = @($rows | Group-Object package_kind | Sort-Object Name | ForEach-Object {
            [pscustomobject]@{ package_kind = $_.Name; count = $_.Count }
        })
    packages = @($rows)
}

$summaryPath = Join-Path $OutDir "summary.json"
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host ("Taito corpus inventory: {0}/{1} supported by Mnemos today; summary: {2}" -f `
        $supportedRows.Count, $rows.Count, $summaryPath)
foreach ($row in $supportedRows) {
    Write-Host ("  [supported] {0,-9} {1} ({2})" -f $row.supported_system, `
            $row.set_id, $row.relative_path) -ForegroundColor Green
}
foreach ($row in $unsupportedRows) {
    Write-Host ("  [uncovered] {0,-28} {1}" -f $row.package_kind, $row.relative_path) `
        -ForegroundColor Yellow
}

if ($RequireAllSupported -and $unsupportedRows.Count -gt 0) {
    exit 1
}
exit 0
