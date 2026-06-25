#!/usr/bin/env pwsh
# Data-gated Taito F2 corpus smoke runner.
#
# ROM zips are never committed. Point this at a single zip with
# MNEMOS_TAITO_F2_ROM, or at a directory of zips with MNEMOS_TAITO_F2_SET_DIR.
# The runner combines each local zip with the matching checked-in game manifest
# inside build/scratch so ordinary set zips do not need to be modified in place.
# Optional screenshot SHA-256 pins can be supplied through -ExpectedHashes or
# MNEMOS_TAITO_F2_GOLDENS once a real-ROM frame has been accepted as golden.
# Optional rendered-audio evidence can be enabled with -AudioProbe or
# MNEMOS_TAITO_F2_AUDIO_PROBE=1; -RequireAudioEvidence fails sets whose probe is
# missing, invalid, or silent.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string]$Rom = "",
    [string]$RomDir = "",
    [string]$ExpectedHashes = "",
    [int]$Frames = 1200,
    [int]$MaxSets = 0,
    [switch]$IncludeAllZips,
    [switch]$Recurse,
    [switch]$RequireGoldens,
    [switch]$RequireManifestCoverage,
    [switch]$RequireFeatureEvidence,
    [switch]$GameplayProbe,
    [int]$GameplayFrames = 1800,
    [string[]]$GameplayPress = @("select@30+4", "start@180+8", "a@360+60"),
    [switch]$AudioProbe,
    [int]$AudioFrames = 1800,
    [string[]]$AudioPress = @("select@30+4", "start@180+8"),
    [switch]$RequireAudioEvidence
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

function Get-TaitoF2KnownRosterDebt {
    param([Parameter(Mandatory = $true)][object[]]$ManifestProfiles)

    $declared = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($profile in $ManifestProfiles) {
        if (-not [string]::IsNullOrWhiteSpace([string]$profile.set)) {
            [void]$declared.Add([string]$profile.set)
        }
    }

    $knownUnmanifestedSets = @(
        "finalb",
        "megab",
        "cameltry",
        "cameltrya",
        "ssi",
        "majest12",
        "mjnquest",
        "yuyugogo",
        "koshien",
        "yesnoj",
        "qjinsei",
        "qcrayon",
        "qcrayon2",
        "driftout"
    )
    $missingSets = @($knownUnmanifestedSets |
        Where-Object { -not $declared.Contains($_) } |
        Sort-Object)

    $missingProfileGates = @()
    if ($missingSets.Count -gt 0) {
        $missingProfileGates = @(
            "tc0030cmd_cchip_protection",
            "tc8521ap_rp5c01_rtc_status",
            "printer_status_input",
            "analog_paddle_input",
            "mahjong_key_matrix_input",
            "alternate_ym2203_okim6295_sound",
            "yuyugogo_jinsei_crayon_sprite_extension_profiles",
            "driftout_cameltry_roz_flip_offsets"
        )
    }

    return [pscustomobject]@{
        total_known_unmanifested_sets = $knownUnmanifestedSets.Count
        missing_sets = @($missingSets)
        missing_profile_gates = @($missingProfileGates)
        evidence = "reported roster debt; add manifests plus board/device sidecars before runtime claims"
    }
}

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Get-ObjectStringValue {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string[]]$Names
    )
    foreach ($name in $Names) {
        $property = $Object.PSObject.Properties[$name]
        if ($null -ne $property -and $null -ne $property.Value) {
            return [string]$property.Value
        }
    }
    return $null
}

function Add-ExpectedHash {
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.Generic.Dictionary[string, string]]$Map,
        [string]$SetId,
        [string]$Hash
    )
    if ([string]::IsNullOrWhiteSpace($SetId) -or [string]::IsNullOrWhiteSpace($Hash)) {
        return
    }
    $normalized = $Hash.Trim().ToLowerInvariant()
    if ($normalized -notmatch '^[0-9a-f]{64}$') {
        throw "Invalid Taito F2 screenshot SHA-256 for set '$SetId': $Hash"
    }
    $Map[$SetId.Trim()] = $normalized
}

function Get-ExpectedHashMap {
    param([string]$Path)
    $map = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ,$map
    }

    $resolved = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Taito F2 golden hash file not found: $Path"
    }

    $json = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
    if ($json -is [array]) {
        foreach ($row in $json) {
            $setId = Get-ObjectStringValue -Object $row -Names @("set", "set_id", "id", "name")
            $hash = Get-ObjectStringValue -Object $row -Names @(
                "screenshot_sha256",
                "expected_screenshot_sha256",
                "sha256")
            Add-ExpectedHash -Map $map -SetId $setId -Hash $hash
        }
        return ,$map
    }

    $singleSet = Get-ObjectStringValue -Object $json -Names @("set", "set_id", "id", "name")
    $singleHash = Get-ObjectStringValue -Object $json -Names @(
        "screenshot_sha256",
        "expected_screenshot_sha256",
        "sha256")
    if ($null -ne $singleSet -and $null -ne $singleHash) {
        Add-ExpectedHash -Map $map -SetId $singleSet -Hash $singleHash
        return ,$map
    }

    foreach ($property in $json.PSObject.Properties) {
        if ($property.Value -is [string]) {
            Add-ExpectedHash -Map $map -SetId $property.Name -Hash $property.Value
        } else {
            $hash = Get-ObjectStringValue -Object $property.Value -Names @(
                "screenshot_sha256",
                "expected_screenshot_sha256",
                "sha256")
            Add-ExpectedHash -Map $map -SetId $property.Name -Hash $hash
        }
    }
    return ,$map
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
        Write-Warning "Taito F2 ROM path not found: $Path"
    }
}

function Invoke-Player {
    param(
        [Parameter(Mandatory = $true)][string]$Player,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $stdoutPath = "$LogPath.stdout"
    $stderrPath = "$LogPath.stderr"
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Player @Arguments > $stdoutPath 2> $stderrPath
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousErrorActionPreference
        $combined = ""
        if (Test-Path -LiteralPath $stdoutPath -PathType Leaf) {
            $combined += Get-Content -LiteralPath $stdoutPath -Raw
        }
        if (Test-Path -LiteralPath $stderrPath -PathType Leaf) {
            $combined += Get-Content -LiteralPath $stderrPath -Raw
        }
        Set-Content -LiteralPath $LogPath -Value $combined -Encoding utf8
        return $exitCode
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    }
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

function Get-TomlStringValue {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$Key
    )
    $pattern = '^\s*' + [regex]::Escape($Key) + '\s*=\s*"([^"]+)"'
    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        $match = [regex]::Match($line, $pattern)
        if ($match.Success) {
            return $match.Groups[1].Value
        }
    }
    return $null
}

function Test-TomlKey {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$Key
    )
    $pattern = '^\s*' + [regex]::Escape($Key) + '\s*='
    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        if ([regex]::IsMatch($line, $pattern)) {
            return $true
        }
    }
    return $false
}

function Test-TomlRegion {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $pattern = '^\s*name\s*=\s*"' + [regex]::Escape($Name) + '"'
    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        if ([regex]::IsMatch($line, $pattern)) {
            return $true
        }
    }
    return $false
}

function Add-TaitoF2Feature {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Features,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not $Features.Contains($Name)) {
        $Features.Add($Name)
    }
}

function Get-TaitoF2ManifestProfiles {
    param([Parameter(Mandatory = $true)]$ManifestMap)
    $profiles = [System.Collections.Generic.List[object]]::new()
    $knownTc0260DarMaps = @(
        "deadconx", "dinorex", "dondokod", "driftout", "footchmp",
        "growl", "gunfront", "koshien", "liquidk", "megab", "metalb",
        "ninjak", "pulirula", "qcrayon", "qcrayon2", "qjinsei",
        "qzchikyu", "qzquest", "solfigtr", "ssi", "thundfox",
        "yuyugogo"
    )
    foreach ($entry in @($ManifestMap.GetEnumerator() | Sort-Object Key)) {
        $path = [string]$entry.Value
        $map = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_map"
        if ([string]::IsNullOrWhiteSpace($map)) {
            $map = "synthetic"
        }
        $features = [System.Collections.Generic.List[string]]::new()
        $paletteFormat = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_palette_format"
        $spritePolicy = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_sprite_policy"
        $spriteBuffering = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_sprite_buffering"
        $spriteActiveArea = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_sprite_active_area"
        $inputProfile = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_input_profile"
        $ioProfile = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_io_profile"
        $paletteProfile = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_palette_profile"
        $priorityProfile = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_priority_profile"
        $spriteChipPair = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_sprite_chip_pair"
        $soundCommChip = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_sound_comm_chip"
        $videoProfile = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_video_profile"
        $auxProfile = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_aux_profile"
        $textGfxSource = Get-TomlStringValue -ManifestPath $path -Key "taito_f2_text_gfx_source"
        $tc0480ScpProfile =
            Get-TomlStringValue -ManifestPath $path -Key "taito_f2_tc0480scp_profile"
        $orientation = Get-TomlStringValue -ManifestPath $path -Key "orientation"
        $usesTc0480Scp = $map -in @("metalb", "footchmp", "deadconx")
        $usesKnownTc0260Dar = $map -in $knownTc0260DarMaps

        Add-TaitoF2Feature -Features $features -Name "tc0140syt_sound_comm"
        Add-TaitoF2Feature -Features $features -Name "ym2610_scene_audio"
        Add-TaitoF2Feature -Features $features -Name "ym2610_fm_lfo_ssgeg_csm"
        Add-TaitoF2Feature -Features $features -Name "ym2610_timer_irq_sound_cpu"
        Add-TaitoF2Feature -Features $features -Name "ym2610_pan_level_mix"
        Add-TaitoF2Feature -Features $features -Name "ym2610_audio_savestate_phase"
        Add-TaitoF2Feature -Features $features -Name "z80_sound_bank_latch_semantics"
        Add-TaitoF2Feature -Features $features -Name "z80_nmi_acceptance_trace"
        Add-TaitoF2Feature -Features $features -Name "sound_cpu_reset_control_line"
        Add-TaitoF2Feature -Features $features -Name "tc0140syt_reset_callback_trace"
        Add-TaitoF2Feature -Features $features -Name "tc0140syt_nibble_phase_nmi_ack"
        Add-TaitoF2Feature -Features $features -Name "tc0140syt_tc0530syc_sound_comm_profile"
        Add-TaitoF2Feature -Features $features -Name "tc0140syt_command_consumption_trace"
        Add-TaitoF2Feature -Features $features -Name "tc0140syt_reply_clear_trace"
        if ($soundCommChip -eq "tc0530syc") {
            Add-TaitoF2Feature -Features $features -Name "tc0530syc_runtime_support"
        }
        Add-TaitoF2Feature -Features $features -Name "f2_board_clock_profile_12m_4m_8m"
        Add-TaitoF2Feature -Features $features -Name "m68k_z80_ym2610_interleave"
        Add-TaitoF2Feature -Features $features -Name "m68k_irq_ack_vector_timing"
        Add-TaitoF2Feature -Features $features -Name "m68k_bus_wait_open_bus_width"
        Add-TaitoF2Feature -Features $features -Name "m68k_byte_word_access_width_trace"
        Add-TaitoF2Feature -Features $features -Name "address_map_unmapped_open_bus_sidecar"
        Add-TaitoF2Feature -Features $features -Name "m68k_autovector_irq_level_sidecar"
        Add-TaitoF2Feature -Features $features -Name "m68k_irq5_irq6_vbl_dma_mapping"
        Add-TaitoF2Feature -Features $features -Name "m68k_irq2_placeholder_guard"
        Add-TaitoF2Feature -Features $features -Name "vblank_irq_timing"
        Add-TaitoF2Feature -Features $features -Name "vblank_irq_level_by_board"
        Add-TaitoF2Feature -Features $features -Name "video_frame_timing_raw_presented_area"
        Add-TaitoF2Feature -Features $features -Name "scene_capture_matrix_per_board"
        Add-TaitoF2Feature -Features $features -Name "f2_oracle_registry_entry"
        Add-TaitoF2Feature -Features $features -Name "f2_physical_chip_profile_manifest"
        Add-TaitoF2Feature -Features $features -Name "f2_custom_chip_revision_profile"
        Add-TaitoF2Feature -Features $features -Name "f2_address_map_vs_chip_profile_split"
        if ($usesKnownTc0260Dar) {
            Add-TaitoF2Feature -Features $features -Name "tc0260dar_known_roster_profile"
            Add-TaitoF2Feature -Features $features -Name "tc0260dar_runtime_support"
        }
        Add-TaitoF2Feature -Features $features -Name "board_raw_register_window_sidecars"
        Add-TaitoF2Feature -Features $features -Name "board_input_dip_watchdog"
        Add-TaitoF2Feature -Features $features -Name "io_custom_identity_profile"
        if (-not [string]::IsNullOrWhiteSpace($inputProfile)) {
            Add-TaitoF2Feature -Features $features -Name ("input_profile_" + $inputProfile)
        }
        Add-TaitoF2Feature -Features $features -Name "board_io_output_sidecars"
        Add-TaitoF2Feature -Features $features -Name "aux_peripheral_protection_rtc_profile"
        Add-TaitoF2Feature -Features $features -Name "coin_service_watchdog_reset_semantics"
        Add-TaitoF2Feature -Features $features -Name "watchdog_timer_reset_sidecar"
        Add-TaitoF2Feature -Features $features -Name "watchdog_address_map_write_windows"
        Add-TaitoF2Feature -Features $features -Name "io_device_byte_lane_width_semantics"
        Add-TaitoF2Feature -Features $features -Name "service_dip_mux_readback_trace"
        Add-TaitoF2Feature -Features $features -Name "service_test_input_path"
        Add-TaitoF2Feature -Features $features -Name "cabinet_test_switch_input"
        Add-TaitoF2Feature -Features $features -Name "dip_switch_defaults_by_set"
        if ($auxProfile -eq "tc0030cmd_cchip") {
            Add-TaitoF2Feature -Features $features -Name "tc0030cmd_cchip_runtime_support"
        } elseif ($auxProfile -eq "rtc") {
            Add-TaitoF2Feature -Features $features -Name "rtc_runtime_support"
        } elseif ($auxProfile -eq "printer") {
            Add-TaitoF2Feature -Features $features -Name "printer_runtime_support"
        } elseif ($auxProfile -eq "rtc_printer") {
            Add-TaitoF2Feature -Features $features -Name "rtc_runtime_support"
            Add-TaitoF2Feature -Features $features -Name "printer_runtime_support"
        }
        Add-TaitoF2Feature -Features $features -Name "rom_region_interleave_decode"
        Add-TaitoF2Feature -Features $features -Name "rom_clone_parent_resolution_matrix"
        Add-TaitoF2Feature -Features $features -Name "savestate_board_audio_video_phase"
        Add-TaitoF2Feature -Features $features -Name "exact_display_timing_visible_area"
        Add-TaitoF2Feature -Features $features -Name "raster_midframe_video_writes"
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_dma_irq_buffer_timing"
        Add-TaitoF2Feature -Features $features -Name "sprite_dma_irq_assert_ack_timing"
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_custom_pair_old_new_versions"
        if ($spriteChipPair -eq "tc0540obn_tc0520tbc") {
            Add-TaitoF2Feature -Features $features -Name "tc0540obn_tc0520tbc_runtime_support"
        }
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_master_extra_scroll"
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_zoom_continuation"
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_control_marker_per_map"
        if ($spriteBuffering -like "partial_delayed*") {
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_partial_buffer_byte_lane_profile"
        }
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_latched_vs_current_ram_sidecars"
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_sprite_disable_flip_markers"
        Add-TaitoF2Feature -Features $features -Name "tc0200obj_offscreen_wrap_clip"
        Add-TaitoF2Feature -Features $features -Name "tc0360pri_priority_blend"
        Add-TaitoF2Feature -Features $features -Name "tc0360pri_raw_register_sidecar"

        if (-not $usesTc0480Scp) {
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_bg_text"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_register_snapshot_sidecar"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_raw_scroll_register_sidecar"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_tile_decode_layout"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_scroll_origin"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_scene_bbox_origin_matrix"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_bg_y_origin"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_text_y_origin"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_rowscroll_colscroll"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_layer_disable_priority"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_text_priority_model"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_text_source_sidecars"
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_flip_offsets"
            if ((Test-TomlKey -ManifestPath $path -Key "taito_f2_tc0100scn_bg_x_offset") -or
                (Test-TomlKey -ManifestPath $path -Key "taito_f2_tc0100scn_text_x_offset")) {
                Add-TaitoF2Feature -Features $features -Name "tc0100scn_manifest_x_offsets"
            }
        }
        if ($usesTc0480Scp) {
            Add-TaitoF2Feature -Features $features -Name "tc0480scp"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_register_snapshot_sidecar"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_raw_control_register_sidecar"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_tile_decode_layout"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_bg_text_offsets"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_scene_bbox_origin_matrix"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_text_source_sidecars"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_rowscroll_colscroll"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_row_zoom_double_width"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_control_snapshot_timing"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_layer_disable_priority"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_priority_model"
            Add-TaitoF2Feature -Features $features -Name "tc0480scp_flip_offsets"
            if (-not [string]::IsNullOrWhiteSpace($tc0480ScpProfile)) {
                Add-TaitoF2Feature -Features $features -Name (
                    "tc0480scp_profile_" + $tc0480ScpProfile)
            }
        }
        if ($map -eq "thundfox") {
            Add-TaitoF2Feature -Features $features -Name "dual_tc0100scn"
            Add-TaitoF2Feature -Features $features -Name "dual_tc0100scn_priority_merge"
            Add-TaitoF2Feature -Features $features -Name "dual_tc0100scn_secondary_register_sidecar"
        }
        if ($map -in @("dondokod", "pulirula")) {
            Add-TaitoF2Feature -Features $features -Name "roz"
            Add-TaitoF2Feature -Features $features -Name "roz_raw_control_register_sidecar"
            Add-TaitoF2Feature -Features $features -Name "roz_fixed_point_offsets"
            Add-TaitoF2Feature -Features $features -Name "roz_priority_palette_bank"
            Add-TaitoF2Feature -Features $features -Name "roz_wrap_clip_flip_semantics"
            Add-TaitoF2Feature -Features $features -Name "roz_flip_scene_offsets"
            if ((Test-TomlKey -ManifestPath $path -Key "taito_f2_roz_x_offset") -or
                (Test-TomlKey -ManifestPath $path -Key "taito_f2_roz_y_offset")) {
                Add-TaitoF2Feature -Features $features -Name "roz_manifest_offsets"
            }
        }
        if ($map -eq "dondokod") {
            Add-TaitoF2Feature -Features $features -Name "tc0280grd_dual_chip_profile"
            Add-TaitoF2Feature -Features $features -Name "tc0280grd_multi_chip_register_pair"
        } elseif ($map -eq "pulirula") {
            Add-TaitoF2Feature -Features $features -Name "tc0430grw_chip_profile"
        }
        if ($map -in @("growl", "gunfront", "ninjak", "solfigtr", "footchmp", "deadconx")) {
            Add-TaitoF2Feature -Features $features -Name "tc0190fmc_banked_sprites"
            Add-TaitoF2Feature -Features $features -Name "tc0190fmc_raw_bank_register_sidecar"
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_blank_record_bank_guard"
            Add-TaitoF2Feature -Features $features -Name "tc0190fmc_bank_latch_timing"
        }
        if ($map -in @("pulirula", "dinorex")) {
            Add-TaitoF2Feature -Features $features -Name "sprite_extension_ram"
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_extension_code_timing"
        }
        if ($map -in @("qzchikyu", "qzquest", "quizhq", "qtorimon")) {
            Add-TaitoF2Feature -Features $features -Name "shifted_quiz_io"
            Add-TaitoF2Feature -Features $features -Name "tc0220ioc_shifted_quiz_mux"
        }
        if ($map -in @("quizhq", "qtorimon")) {
            Add-TaitoF2Feature -Features $features -Name "tc0100scn_program_region_text_1bpp"
        }
        if (-not [string]::IsNullOrWhiteSpace($textGfxSource)) {
            Add-TaitoF2Feature -Features $features -Name ("text_gfx_source_" + $textGfxSource)
        }
        if ($map -in @("growl", "ninjak")) {
            Add-TaitoF2Feature -Features $features -Name "four_player_io"
            Add-TaitoF2Feature -Features $features -Name "four_player_coin_counter_lockout_outputs"
            Add-TaitoF2Feature -Features $features -Name "four_player_coin_counter_slots"
            Add-TaitoF2Feature -Features $features -Name "four_player_service_test_mux"
        }
        if ($map -in @("growl", "solfigtr")) {
            Add-TaitoF2Feature -Features $features -Name "tmp82c265_panel_coin_outputs"
        } elseif ($map -eq "ninjak") {
            Add-TaitoF2Feature -Features $features -Name "te7750_quad_player_mux"
        } elseif ($map -notin @("qzchikyu", "qzquest", "quizhq", "qtorimon")) {
            Add-TaitoF2Feature -Features $features -Name "tc0220ioc_tc0510nio_input_mux"
        }
        if ($orientation -eq "vertical") {
            Add-TaitoF2Feature -Features $features -Name "vertical_presentation"
            Add-TaitoF2Feature -Features $features -Name "vertical_raw_presented_capture"
        }
        if (-not [string]::IsNullOrWhiteSpace($paletteFormat)) {
            Add-TaitoF2Feature -Features $features -Name ("palette_" + $paletteFormat)
            Add-TaitoF2Feature -Features $features -Name "tc0110pcr_palette_readback"
            Add-TaitoF2Feature -Features $features -Name "tc0110pcr_tc0260dar_tc0070rgb_palette_profile"
        }
        if (-not [string]::IsNullOrWhiteSpace($spritePolicy)) {
            Add-TaitoF2Feature -Features $features -Name ("tc0200obj_policy_" + $spritePolicy)
        }
        if (-not [string]::IsNullOrWhiteSpace($spriteBuffering)) {
            Add-TaitoF2Feature -Features $features -Name ("tc0200obj_buffering_" + $spriteBuffering)
        }
        if (-not [string]::IsNullOrWhiteSpace($spriteActiveArea)) {
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_active_area_marker"
        }
        if ((Test-TomlKey -ManifestPath $path -Key "taito_f2_sprite_hide_pixels") -or
            (Test-TomlKey -ManifestPath $path -Key "taito_f2_sprite_flip_hide_pixels")) {
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_hide_pixel_offsets"
        }
        if (Test-TomlRegion -ManifestPath $path -Name "tiles") {
            Add-TaitoF2Feature -Features $features -Name "tile_rom_region_layout_provenance"
        }
        if (Test-TomlRegion -ManifestPath $path -Name "tiles_secondary") {
            Add-TaitoF2Feature -Features $features -Name "secondary_tile_rom_region_layout_provenance"
        }
        if (Test-TomlRegion -ManifestPath $path -Name "sprites") {
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_decoded_object_sidecar"
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_sprite_gfx_decode_layout"
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_sprite_rom_region_order"
            Add-TaitoF2Feature -Features $features -Name "tc0200obj_palette_bank_origin"
        }
        if (Test-TomlRegion -ManifestPath $path -Name "audiocpu") {
            Add-TaitoF2Feature -Features $features -Name "z80_sound_program"
            Add-TaitoF2Feature -Features $features -Name "z80_sound_rom_bank_region_layout"
            Add-TaitoF2Feature -Features $features -Name "z80_sound_bank_mask_page_size_by_board"
            Add-TaitoF2Feature -Features $features -Name "z80_sound_bank_switch_trace"
            Add-TaitoF2Feature -Features $features -Name "tc0140syt_to_ym2610_command_latency_trace"
        }
        if (Test-TomlRegion -ManifestPath $path -Name "adpcma") {
            Add-TaitoF2Feature -Features $features -Name "ym2610_adpcma_samples"
            Add-TaitoF2Feature -Features $features -Name "ym2610_adpcma_channel_cadence"
            Add-TaitoF2Feature -Features $features -Name "ym2610_adpcma_rekey_trace"
            Add-TaitoF2Feature -Features $features -Name "ym2610_scene_loop_waveform_compare"
            Add-TaitoF2Feature -Features $features -Name "ym2610_dac_route_filter_profile"
        }
        if (Test-TomlRegion -ManifestPath $path -Name "adpcmb") {
            Add-TaitoF2Feature -Features $features -Name "ym2610_adpcmb_samples"
            Add-TaitoF2Feature -Features $features -Name "ym2610_adpcmb_control_writes"
            Add-TaitoF2Feature -Features $features -Name "ym2610_adpcmb_loop_end_cadence"
        }

        $profiles.Add([pscustomobject]@{
            set = [string]$entry.Key
            map = $map
            parent = Get-ManifestParent -ManifestPath $path
            palette_format = $paletteFormat
            sprite_policy = $spritePolicy
            sprite_buffering = $spriteBuffering
            sprite_active_area = $spriteActiveArea
            input_profile = $inputProfile
            io_profile = $ioProfile
            palette_profile = $paletteProfile
            priority_profile = $priorityProfile
            sprite_chip_pair = $spriteChipPair
            sound_comm_chip = $soundCommChip
            video_profile = $videoProfile
            aux_profile = $auxProfile
            text_gfx_source = $textGfxSource
            tc0480scp_profile = $tc0480ScpProfile
            features = @($features)
            manifest = $path
        })
    }
    return @($profiles)
}

function Get-TaitoF2ManifestCoverage {
    param(
        [Parameter(Mandatory = $true)][object[]]$ManifestProfiles,
        [Parameter(Mandatory = $true)][object[]]$Results
    )
    $attempted = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $passed = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Results) {
        if ($null -eq $row.set) {
            continue
        }
        [void]$attempted.Add([string]$row.set)
        if ($row.passed) {
            [void]$passed.Add([string]$row.set)
        }
    }

    $variantCoverage = [System.Collections.Generic.List[object]]::new()
    foreach ($group in @($ManifestProfiles | Group-Object map | Sort-Object Name)) {
        $sets = @($group.Group | ForEach-Object { $_.set } | Sort-Object)
        $attemptedSets = @($sets | Where-Object { $attempted.Contains($_) })
        $passedSets = @($sets | Where-Object { $passed.Contains($_) })
        $missingSets = @($sets | Where-Object { -not $attempted.Contains($_) })
        $features = @($group.Group |
            ForEach-Object { $_.features } |
            ForEach-Object { $_ } |
            Sort-Object -Unique)
        $variantCoverage.Add([pscustomobject]@{
            map = $group.Name
            features = @($features)
            sets = $sets
            attempted_sets = $attemptedSets
            passed_sets = $passedSets
            missing_sets = $missingSets
        })
    }

    $featureCoverage = [System.Collections.Generic.List[object]]::new()
    foreach ($feature in @($ManifestProfiles |
                           ForEach-Object { $_.features } |
                           ForEach-Object { $_ } |
                           Sort-Object -Unique)) {
        $sets = @($ManifestProfiles |
            Where-Object { $_.features -contains $feature } |
            ForEach-Object { $_.set } |
            Sort-Object)
        $attemptedSets = @($sets | Where-Object { $attempted.Contains($_) })
        $featureCoverage.Add([pscustomobject]@{
            feature = $feature
            sets = $sets
            attempted_sets = $attemptedSets
            missing_sets = @($sets | Where-Object { -not $attempted.Contains($_) })
        })
    }

    return [pscustomobject]@{
        total_manifest_sets = @($ManifestProfiles).Count
        attempted_sets = @($Results | ForEach-Object { $_.set } | Sort-Object -Unique)
        passed_sets = @($Results | Where-Object { $_.passed } |
            ForEach-Object { $_.set } | Sort-Object -Unique)
        missing_sets = @($ManifestProfiles |
            Where-Object { -not $attempted.Contains($_.set) } |
            ForEach-Object { $_.set } |
            Sort-Object)
        variant_maps = @($variantCoverage)
        feature_gates = @($featureCoverage)
    }
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

function Resolve-TaitoF2NestedZipCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$ManifestMap
    )
    $archive = $null
    try {
        $archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
        foreach ($entry in $archive.Entries) {
            if ([string]::IsNullOrEmpty($entry.Name) -or
                -not $entry.Name.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
            $setId = Resolve-TaitoF2ManifestId -Path $entry.FullName -ManifestMap $ManifestMap
            if ($null -ne $setId -or $IncludeAllZips) {
                if ($null -eq $setId) {
                    $setId = [System.IO.Path]::GetFileNameWithoutExtension($entry.Name)
                }
                return [pscustomobject]@{
                    SetId = $setId
                    EntryName = $entry.FullName
                }
            }
        }
    } catch {
        Write-Warning "Taito F2 archive could not be inspected for nested set zips: $Path"
    } finally {
        if ($null -ne $archive) {
            $archive.Dispose()
        }
    }
    return $null
}

function Resolve-TaitoF2RomCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$ManifestMap
    )
    $setId = Resolve-TaitoF2ManifestId -Path $Path -ManifestMap $ManifestMap
    if ($null -ne $setId) {
        return [pscustomobject]@{
            SetId = $setId
            NestedEntry = $null
        }
    }

    $nested = Resolve-TaitoF2NestedZipCandidate -Path $Path -ManifestMap $ManifestMap
    if ($null -ne $nested) {
        return [pscustomobject]@{
            SetId = $nested.SetId
            NestedEntry = $nested.EntryName
        }
    }

    if ($IncludeAllZips) {
        return [pscustomobject]@{
            SetId = [System.IO.Path]::GetFileNameWithoutExtension($Path)
            NestedEntry = $null
        }
    }
    return $null
}

function Find-TaitoF2SiblingSetCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$ChildPath,
        [Parameter(Mandatory = $true)][string]$SetId,
        [Parameter(Mandatory = $true)]$ManifestMap
    )
    $dir = [System.IO.Path]::GetDirectoryName($ChildPath)
    if ([string]::IsNullOrWhiteSpace($dir) -or -not (Test-Path -LiteralPath $dir -PathType Container)) {
        return $null
    }

    foreach ($zip in Get-ChildItem -LiteralPath $dir -Filter "*.zip" -File |
             Sort-Object FullName) {
        if ([string]::Equals($zip.FullName, $ChildPath,
                             [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $candidate = Resolve-TaitoF2RomCandidate -Path $zip.FullName -ManifestMap $ManifestMap
        if ($null -ne $candidate -and
            [string]::Equals($candidate.SetId, $SetId,
                             [System.StringComparison]::OrdinalIgnoreCase)) {
            return [pscustomobject]@{
                Path = $zip.FullName
                NestedEntry = $candidate.NestedEntry
            }
        }
    }

    return $null
}

function Test-TaitoF2ZipCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$ManifestMap
    )
    if ($IncludeAllZips) {
        return $true
    }
    return $null -ne (Resolve-TaitoF2RomCandidate -Path $Path -ManifestMap $ManifestMap)
}

function Get-ManifestParent {
    param([Parameter(Mandatory = $true)][string]$ManifestPath)
    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        $match = [regex]::Match($line, '^\s*parent\s*=\s*"([^"]+)"')
        if ($match.Success) {
            return $match.Groups[1].Value
        }
    }
    return $null
}

function New-ZipWithManifest {
    param(
        [Parameter(Mandatory = $true)][string]$SourceZip,
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$OutputZip
    )
    if (Test-Path -LiteralPath $OutputZip) {
        Remove-Item -LiteralPath $OutputZip -Force
    }
    $source = [System.IO.Compression.ZipFile]::OpenRead($SourceZip)
    $dest = [System.IO.Compression.ZipFile]::Open(
        $OutputZip, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($entry in $source.Entries) {
            if ([string]::IsNullOrEmpty($entry.Name)) {
                continue
            }
            if ([string]::Equals($entry.FullName, "game.toml",
                                 [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
            $newEntry = $dest.CreateEntry($entry.FullName,
                                          [System.IO.Compression.CompressionLevel]::Optimal)
            $inStream = $entry.Open()
            $outStream = $newEntry.Open()
            try {
                $inStream.CopyTo($outStream)
            } finally {
                $outStream.Dispose()
                $inStream.Dispose()
            }
        }

        $manifestEntry = $dest.CreateEntry(
            "game.toml", [System.IO.Compression.CompressionLevel]::Optimal)
        $manifestIn = [System.IO.File]::OpenRead($ManifestPath)
        $manifestOut = $manifestEntry.Open()
        try {
            $manifestIn.CopyTo($manifestOut)
        } finally {
            $manifestOut.Dispose()
            $manifestIn.Dispose()
        }
    } finally {
        $dest.Dispose()
        $source.Dispose()
    }
}

function Expand-ZipEntryToFile {
    param(
        [Parameter(Mandatory = $true)][string]$SourceZip,
        [Parameter(Mandatory = $true)][string]$EntryName,
        [Parameter(Mandatory = $true)][string]$OutputZip
    )
    if (Test-Path -LiteralPath $OutputZip) {
        Remove-Item -LiteralPath $OutputZip -Force
    }
    $source = [System.IO.Compression.ZipFile]::OpenRead($SourceZip)
    try {
        $entry = $source.GetEntry($EntryName)
        if ($null -eq $entry) {
            throw "Nested zip entry '$EntryName' not found in '$SourceZip'."
        }
        $inStream = $entry.Open()
        $outStream = [System.IO.File]::Create($OutputZip)
        try {
            $inStream.CopyTo($outStream)
        } finally {
            $outStream.Dispose()
            $inStream.Dispose()
        }
    } finally {
        $source.Dispose()
    }
}

function Get-StagedSourceZip {
    param(
        [Parameter(Mandatory = $true)]$Candidate,
        [Parameter(Mandatory = $true)][string]$OutputZip
    )
    if ($null -eq $Candidate.NestedEntry) {
        return $Candidate.Path
    }
    Expand-ZipEntryToFile -SourceZip $Candidate.Path -EntryName $Candidate.NestedEntry -OutputZip $OutputZip
    return $OutputZip
}

function Test-PpmNonBlank {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 16 -or $bytes[0] -ne [byte][char]'P' -or $bytes[1] -ne [byte][char]'6') {
        return $false
    }
    $lineFeeds = 0
    $payloadStart = -1
    for ($i = 0; $i -lt $bytes.Length; ++$i) {
        if ($bytes[$i] -eq 10) {
            ++$lineFeeds
            if ($lineFeeds -eq 3) {
                $payloadStart = $i + 1
                break
            }
        }
    }
    if ($payloadStart -lt 0 -or $payloadStart + 6 -gt $bytes.Length) {
        return $false
    }
    $r = $bytes[$payloadStart]
    $g = $bytes[$payloadStart + 1]
    $b = $bytes[$payloadStart + 2]
    for ($i = $payloadStart + 3; $i + 2 -lt $bytes.Length; $i += 3) {
        if ($bytes[$i] -ne $r -or $bytes[$i + 1] -ne $g -or $bytes[$i + 2] -ne $b) {
            return $true
        }
    }
    return $false
}

function Read-Le16 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    return ([int]$Bytes[$Offset]) -bor ([int]$Bytes[$Offset + 1] -shl 8)
}

function Read-Le32 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    return ([int64]$Bytes[$Offset]) -bor
        ([int64]$Bytes[$Offset + 1] -shl 8) -bor
        ([int64]$Bytes[$Offset + 2] -shl 16) -bor
        ([int64]$Bytes[$Offset + 3] -shl 24)
}

function Read-LeS16 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    $value = Read-Le16 -Bytes $Bytes -Offset $Offset
    if ($value -ge 0x8000) {
        return [int]($value - 0x10000)
    }
    return [int]$value
}

function New-MissingAudioSummary {
    param([string]$BasePath = "")
    $wavPath = if ([string]::IsNullOrWhiteSpace($BasePath)) { $null } else { "$BasePath.rendered.wav" }
    $manifestPath = if ([string]::IsNullOrWhiteSpace($BasePath)) { $null } else { "$BasePath.audio.json" }
    $tracePath = if ([string]::IsNullOrWhiteSpace($BasePath)) { $null } else { "$BasePath.rendered_audio.json" }
    return [pscustomobject]@{
        present = $false
        path = $wavPath
        valid = $false
        error = $null
        manifest_present = $false
        manifest = $manifestPath
        trace_present = $false
        trace = $tracePath
        trace_valid = $false
        trace_frames = 0
        trace_captured_frames = 0
        trace_sample_rate = 0
        trace_peak_abs = 0
        adpcma_active_frames = 0
        adpcma_active_masks = @()
        adpcma_channel_stats = @()
        adpcma_channel_stats_count = 0
        adpcma_active_channel_count = 0
        adpcma_channel_dropout_suspect = $false
        adpcma_longest_silence_after_active_frames_max = 0
        adpcma_longest_silence_after_active_ms_max = 0
        adpcma_key_on_writes = 0
        adpcma_key_off_writes = 0
        adpcma_key_on_masks = @()
        adpcma_key_on_frames = @()
        adpcma_rekey_trace_present = $false
        adpcma_end_events_max = 0
        adpcma_rom_underruns_max = 0
        adpcmb_active_frames = 0
        adpcmb_control_writes = 0
        adpcmb_control_values = @()
        adpcmb_control_frames = @()
        adpcmb_start_writes = 0
        adpcmb_repeat_start_writes = 0
        adpcmb_start_frames = @()
        adpcmb_end_events_max = 0
        adpcmb_loop_events_max = 0
        adpcmb_rom_underruns_max = 0
        adpcm_any_underrun = $false
        adpcmb_loop_observed = $false
        tc0140syt_present = $false
        tc0140syt_command_nmi_pulses_max = 0
        tc0140syt_command_nmi_pulse_count = 0
        tc0140syt_command_nmi_pulse_frames = @()
        tc0140syt_command_pending_frames = 0
        tc0140syt_command_pending_masks = @()
        tc0140syt_command_latches = @()
        tc0140syt_command_write_count_max = 0
        tc0140syt_command_read_count_max = 0
        tc0140syt_command_read_frames = @()
        tc0140syt_to_adpcma_keyon_latency_observed = $false
        tc0140syt_to_adpcma_keyon_latency_min_frames = 0
        tc0140syt_to_adpcma_keyon_latency_max_frames = 0
        tc0140syt_to_adpcma_keyon_latency_frames = @()
        tc0140syt_reply_write_count_max = 0
        tc0140syt_reply_read_count_max = 0
        tc0140syt_clear_count_max = 0
        z80_sound_cpu_present = $false
        z80_nmi_accept_count_max = 0
        z80_nmi_accept_frames = @()
        z80_irq_accept_count_max = 0
        chip_count = 0
        stored_sample_count = 0
        register_count = 0
        sample_rate = 0
        channels = 0
        bits_per_sample = 0
        frames = 0
        duration_ms = 0
        nonzero_frames = 0
        peak_abs = 0
        mean_abs = 0.0
        window_ms = 100
        windows = 0
        active_windows = 0
        silent_windows = 0
        active_duty_cycle = 0.0
        longest_active_run_ms = 0
        longest_silent_run_ms = 0
        longest_silence_after_first_active_ms = 0
        dropout_suspect = $false
    }
}

function Get-RenderedAudioTraceSummary {
    param([Parameter(Mandatory = $true)][string]$TracePath)
    $summary = [pscustomobject]@{
        present = $false
        valid = $false
        frames = 0
        captured_frames = 0
        sample_rate = 0
        peak_abs = 0
        audio_metric_nonzero_frames = 0
        audio_metric_mean_peak_abs = 0.0
        audio_metric_window_ms = 100
        audio_metric_windows = 0
        audio_metric_active_windows = 0
        audio_metric_silent_windows = 0
        audio_metric_active_duty_cycle = 0.0
        audio_metric_longest_active_run_ms = 0
        audio_metric_longest_silent_run_ms = 0
        audio_metric_longest_silence_after_first_active_ms = 0
        audio_metric_dropout_suspect = $false
        adpcma_active_frames = 0
        adpcma_active_masks = @()
        adpcma_channel_stats = @()
        adpcma_channel_stats_count = 0
        adpcma_active_channel_count = 0
        adpcma_channel_dropout_suspect = $false
        adpcma_longest_silence_after_active_frames_max = 0
        adpcma_longest_silence_after_active_ms_max = 0
        adpcma_key_on_writes = 0
        adpcma_key_off_writes = 0
        adpcma_key_on_masks = @()
        adpcma_key_on_frames = @()
        adpcma_rekey_trace_present = $false
        adpcma_end_events_max = 0
        adpcma_rom_underruns_max = 0
        adpcmb_active_frames = 0
        adpcmb_control_writes = 0
        adpcmb_control_values = @()
        adpcmb_control_frames = @()
        adpcmb_start_writes = 0
        adpcmb_repeat_start_writes = 0
        adpcmb_start_frames = @()
        adpcmb_end_events_max = 0
        adpcmb_loop_events_max = 0
        adpcmb_rom_underruns_max = 0
        adpcm_any_underrun = $false
        adpcmb_loop_observed = $false
        tc0140syt_present = $false
        tc0140syt_command_nmi_pulses_max = 0
        tc0140syt_command_nmi_pulse_count = 0
        tc0140syt_command_nmi_pulse_frames = @()
        tc0140syt_command_pending_frames = 0
        tc0140syt_command_pending_masks = @()
        tc0140syt_command_latches = @()
        tc0140syt_command_write_count_max = 0
        tc0140syt_command_read_count_max = 0
        tc0140syt_command_read_frames = @()
        tc0140syt_to_adpcma_keyon_latency_observed = $false
        tc0140syt_to_adpcma_keyon_latency_min_frames = 0
        tc0140syt_to_adpcma_keyon_latency_max_frames = 0
        tc0140syt_to_adpcma_keyon_latency_frames = @()
        tc0140syt_reply_write_count_max = 0
        tc0140syt_reply_read_count_max = 0
        tc0140syt_clear_count_max = 0
        z80_sound_cpu_present = $false
        z80_nmi_accept_count_max = 0
        z80_nmi_accept_frames = @()
        z80_irq_accept_count_max = 0
    }
    if (-not (Test-Path -LiteralPath $TracePath -PathType Leaf)) {
        return $summary
    }

    $summary.present = $true
    $json = Get-Content -LiteralPath $TracePath -Raw | ConvertFrom-Json
    if ([string]$json.schema -ne "mnemos.rendered_audio_trace/1") {
        return $summary
    }

    $summary.valid = $true
    $summary.captured_frames = [int64]$json.captured_frames
    $summary.sample_rate = [int]$json.sample_rate
    $activeMasks = [System.Collections.Generic.List[string]]::new()
    $channelActiveFrames = [int[]]::new(6)
    $channelCurrentActive = [int[]]::new(6)
    $channelCurrentSilent = [int[]]::new(6)
    $channelLongestActive = [int[]]::new(6)
    $channelLongestSilentAfterActive = [int[]]::new(6)
    $channelSeenActive = [bool[]]::new(6)
    $channelEndEventsMax = [int64[]]::new(6)
    $channelRomUnderrunsMax = [int64[]]::new(6)
    $channelKeyOnWrites = [int[]]::new(6)
    $channelKeyOffWrites = [int[]]::new(6)
    $channelLastKeyOnFrame = [int[]]::new(6)
    $channelLongestKeyOnGap = [int[]]::new(6)
    $channelKeyOnFrames = @()
    for ($channel = 0; $channel -lt 6; ++$channel) {
        $channelLastKeyOnFrame[$channel] = -1
        $channelKeyOnFrames += ,([System.Collections.Generic.List[int]]::new())
    }
    $adpcmaKeyOnMasks = [System.Collections.Generic.List[string]]::new()
    $adpcmaKeyOnFrames = [System.Collections.Generic.List[int]]::new()
    $adpcmbControlValues = [System.Collections.Generic.List[string]]::new()
    $adpcmbControlFrames = [System.Collections.Generic.List[int]]::new()
    $adpcmbStartFrames = [System.Collections.Generic.List[int]]::new()
    $tc0140sytCommandNmiFrames = [System.Collections.Generic.List[int]]::new()
    $tc0140sytCommandPendingMasks = [System.Collections.Generic.List[string]]::new()
    $tc0140sytCommandLatches = [System.Collections.Generic.List[string]]::new()
    $audioMetricActiveWindows = [System.Collections.Generic.HashSet[int]]::new()
    [int64]$audioMetricPeakSum = 0
    [int]$audioMetricWindowFrames = 6
    [int]$audioMetricActiveThreshold = 8
    [int64]$lastTc0140sytCommandNmi = -1
    [int64]$lastTc0140sytCommandReads = -1
    $tc0140sytCommandReadFrames = [System.Collections.Generic.List[int]]::new()
    [int64]$lastZ80NmiAccepts = -1
    $z80NmiAcceptFrames = [System.Collections.Generic.List[int]]::new()
    foreach ($frame in @($json.frame_metrics)) {
        $frameNumber = [int]$frame.frame
        ++$summary.frames
        $framePeak = [int]$frame.peak_abs
        if ($framePeak -gt $summary.peak_abs) {
            $summary.peak_abs = $framePeak
        }
        if ($framePeak -gt 0) {
            ++$summary.audio_metric_nonzero_frames
        }
        $audioMetricPeakSum += $framePeak
        if ($framePeak -gt $audioMetricActiveThreshold) {
            [int]$traceFrameIndex = [Math]::Max(0, [int]$summary.frames - 1)
            [void]$audioMetricActiveWindows.Add(
                [int][Math]::Floor($traceFrameIndex / [double]$audioMetricWindowFrames))
        }

        [bool]$adpcmaActive = $false
        [bool]$adpcmbActive = $false
        [int64]$adpcmaMask = 0
        [int64]$tc0140sytCommandNmi = -1
        [int64]$tc0140sytCommandPending = 0
        [int64]$tc0140sytM2s0 = -1
        [int64]$tc0140sytM2s1 = -1
        [int64]$tc0140sytCommandWrites = 0
        [int64]$tc0140sytCommandReads = 0
        [int64]$tc0140sytReplyWrites = 0
        [int64]$tc0140sytReplyReads = 0
        [int64]$tc0140sytClearCount = -1
        [int64]$z80NmiAccepts = -1
        [int64]$z80IrqAccepts = -1
        foreach ($chip in @($frame.chips)) {
            $partNumber = [string]$chip.part_number
            $isTc0140syt = $partNumber -eq "TC0140SYT"
            $isZ80 = $partNumber -eq "Z80"
            if ($isTc0140syt) {
                $summary.tc0140syt_present = $true
            }
            if ($isZ80) {
                $summary.z80_sound_cpu_present = $true
            }
            foreach ($reg in @($chip.registers)) {
                $name = [string]$reg.name
                $value = [int64]$reg.value
                if ($name -eq "ADPCMA_ACTIVE") {
                    $adpcmaMask = $value
                    if ($value -ne 0) {
                        $adpcmaActive = $true
                        $activeMasks.Add(("0x{0:X2}" -f $value))
                    }
                } elseif ($name -match "^ADPCMA_CH([0-5])_END_EVENTS$") {
                    $channel = [int]$Matches[1]
                    if ($value -gt $summary.adpcma_end_events_max) {
                        $summary.adpcma_end_events_max = $value
                    }
                    if ($value -gt $channelEndEventsMax[$channel]) {
                        $channelEndEventsMax[$channel] = $value
                    }
                } elseif ($name -match "^ADPCMA_CH([0-5])_ROM_UNDERRUNS$") {
                    $channel = [int]$Matches[1]
                    if ($value -gt $summary.adpcma_rom_underruns_max) {
                        $summary.adpcma_rom_underruns_max = $value
                    }
                    if ($value -gt $channelRomUnderrunsMax[$channel]) {
                        $channelRomUnderrunsMax[$channel] = $value
                    }
                } elseif ($name -eq "ADPCMB_ACTIVE") {
                    if ($value -ne 0) {
                        $adpcmbActive = $true
                    }
                } elseif ($name -eq "ADPCMB_END_EVENTS") {
                    if ($value -gt $summary.adpcmb_end_events_max) {
                        $summary.adpcmb_end_events_max = $value
                    }
                } elseif ($name -eq "ADPCMB_LOOP_EVENTS") {
                    if ($value -gt $summary.adpcmb_loop_events_max) {
                        $summary.adpcmb_loop_events_max = $value
                    }
                } elseif ($name -eq "ADPCMB_ROM_UNDERRUNS") {
                    if ($value -gt $summary.adpcmb_rom_underruns_max) {
                        $summary.adpcmb_rom_underruns_max = $value
                    }
                } elseif ($isTc0140syt -and $name -eq "CMDNMI") {
                    $tc0140sytCommandNmi = $value
                    if ($value -gt $summary.tc0140syt_command_nmi_pulses_max) {
                        $summary.tc0140syt_command_nmi_pulses_max = $value
                    }
                } elseif ($isTc0140syt -and $name -eq "M2SPEND") {
                    $tc0140sytCommandPending = $value
                } elseif ($isTc0140syt -and $name -eq "M2S0") {
                    $tc0140sytM2s0 = $value
                } elseif ($isTc0140syt -and $name -eq "M2S1") {
                    $tc0140sytM2s1 = $value
                } elseif ($isTc0140syt -and $name -match "^CMDW[01]$") {
                    $tc0140sytCommandWrites += $value
                } elseif ($isTc0140syt -and $name -match "^CMDR[01]$") {
                    $tc0140sytCommandReads += $value
                } elseif ($isTc0140syt -and $name -match "^RPLW[01]$") {
                    $tc0140sytReplyWrites += $value
                } elseif ($isTc0140syt -and $name -match "^RPLR[01]$") {
                    $tc0140sytReplyReads += $value
                } elseif ($isTc0140syt -and $name -eq "CLEAR") {
                    $tc0140sytClearCount = $value
                } elseif ($isZ80 -and $name -eq "NMIACC") {
                    $z80NmiAccepts = $value
                } elseif ($isZ80 -and $name -eq "IRQACC") {
                    $z80IrqAccepts = $value
                }
            }
        }
        if ($tc0140sytCommandNmi -ge 0) {
            if ($lastTc0140sytCommandNmi -lt 0) {
                if ($tc0140sytCommandNmi -gt 0) {
                    $summary.tc0140syt_command_nmi_pulse_count += [int]$tc0140sytCommandNmi
                    $tc0140sytCommandNmiFrames.Add($frameNumber)
                }
            } elseif ($tc0140sytCommandNmi -gt $lastTc0140sytCommandNmi) {
                $summary.tc0140syt_command_nmi_pulse_count +=
                    [int]($tc0140sytCommandNmi - $lastTc0140sytCommandNmi)
                $tc0140sytCommandNmiFrames.Add($frameNumber)
            }
            $lastTc0140sytCommandNmi = $tc0140sytCommandNmi
        }
        if ($tc0140sytCommandPending -ne 0) {
            ++$summary.tc0140syt_command_pending_frames
            $tc0140sytCommandPendingMasks.Add(("0x{0:X2}" -f $tc0140sytCommandPending))
        }
        if ($tc0140sytM2s0 -ge 0) {
            $tc0140sytCommandLatches.Add(("p0=0x{0:X2}" -f $tc0140sytM2s0))
        }
        if ($tc0140sytM2s1 -ge 0) {
            $tc0140sytCommandLatches.Add(("p1=0x{0:X2}" -f $tc0140sytM2s1))
        }
        if ($tc0140sytCommandWrites -gt $summary.tc0140syt_command_write_count_max) {
            $summary.tc0140syt_command_write_count_max = $tc0140sytCommandWrites
        }
        if ($tc0140sytCommandReads -gt $summary.tc0140syt_command_read_count_max) {
            $summary.tc0140syt_command_read_count_max = $tc0140sytCommandReads
        }
        if ($lastTc0140sytCommandReads -lt 0) {
            if ($tc0140sytCommandReads -gt 0) {
                $tc0140sytCommandReadFrames.Add($frameNumber)
            }
        } elseif ($tc0140sytCommandReads -gt $lastTc0140sytCommandReads) {
            $tc0140sytCommandReadFrames.Add($frameNumber)
        }
        $lastTc0140sytCommandReads = $tc0140sytCommandReads
        if ($tc0140sytReplyWrites -gt $summary.tc0140syt_reply_write_count_max) {
            $summary.tc0140syt_reply_write_count_max = $tc0140sytReplyWrites
        }
        if ($tc0140sytReplyReads -gt $summary.tc0140syt_reply_read_count_max) {
            $summary.tc0140syt_reply_read_count_max = $tc0140sytReplyReads
        }
        if ($tc0140sytClearCount -gt $summary.tc0140syt_clear_count_max) {
            $summary.tc0140syt_clear_count_max = $tc0140sytClearCount
        }
        if ($z80NmiAccepts -gt $summary.z80_nmi_accept_count_max) {
            $summary.z80_nmi_accept_count_max = $z80NmiAccepts
        }
        if ($lastZ80NmiAccepts -lt 0) {
            if ($z80NmiAccepts -gt 0) {
                $z80NmiAcceptFrames.Add($frameNumber)
            }
        } elseif ($z80NmiAccepts -gt $lastZ80NmiAccepts) {
            $z80NmiAcceptFrames.Add($frameNumber)
        }
        $lastZ80NmiAccepts = $z80NmiAccepts
        if ($z80IrqAccepts -gt $summary.z80_irq_accept_count_max) {
            $summary.z80_irq_accept_count_max = $z80IrqAccepts
        }
        if ($adpcmaActive) { ++$summary.adpcma_active_frames }
        if ($adpcmbActive) { ++$summary.adpcmb_active_frames }
        foreach ($chipWrites in @($frame.register_writes)) {
            foreach ($write in @($chipWrites.writes)) {
                $port = [int]$write.port
                $value = [int]$write.value
                if ($port -eq 0x100) {
                    $mask = $value -band 0x3F
                    if (($value -band 0x80) -ne 0) {
                        if ($mask -ne 0) {
                            ++$summary.adpcma_key_off_writes
                            for ($channel = 0; $channel -lt 6; ++$channel) {
                                if (($mask -band (1 -shl $channel)) -ne 0) {
                                    ++$channelKeyOffWrites[$channel]
                                }
                            }
                        }
                    } elseif ($mask -ne 0) {
                        ++$summary.adpcma_key_on_writes
                        $adpcmaKeyOnMasks.Add(("0x{0:X2}" -f $mask))
                        $adpcmaKeyOnFrames.Add($frameNumber)
                        for ($channel = 0; $channel -lt 6; ++$channel) {
                            if (($mask -band (1 -shl $channel)) -eq 0) {
                                continue
                            }
                            ++$channelKeyOnWrites[$channel]
                            $channelKeyOnFrames[$channel].Add($frameNumber)
                            if ($channelLastKeyOnFrame[$channel] -ge 0) {
                                $gap = $frameNumber - $channelLastKeyOnFrame[$channel]
                                if ($gap -gt $channelLongestKeyOnGap[$channel]) {
                                    $channelLongestKeyOnGap[$channel] = $gap
                                }
                            }
                            $channelLastKeyOnFrame[$channel] = $frameNumber
                        }
                    }
                } elseif ($port -eq 0x10) {
                    ++$summary.adpcmb_control_writes
                    $adpcmbControlValues.Add(("0x{0:X2}" -f $value))
                    $adpcmbControlFrames.Add($frameNumber)
                    if (($value -band 0x80) -ne 0) {
                        ++$summary.adpcmb_start_writes
                        if (($value -band 0x10) -ne 0) {
                            ++$summary.adpcmb_repeat_start_writes
                        }
                        $adpcmbStartFrames.Add($frameNumber)
                    }
                }
            }
        }
        for ($channel = 0; $channel -lt 6; ++$channel) {
            $active = (($adpcmaMask -band (1 -shl $channel)) -ne 0)
            if ($active) {
                ++$channelActiveFrames[$channel]
                ++$channelCurrentActive[$channel]
                $channelCurrentSilent[$channel] = 0
                $channelSeenActive[$channel] = $true
                if ($channelCurrentActive[$channel] -gt $channelLongestActive[$channel]) {
                    $channelLongestActive[$channel] = $channelCurrentActive[$channel]
                }
            } else {
                $channelCurrentActive[$channel] = 0
                if ($channelSeenActive[$channel]) {
                    ++$channelCurrentSilent[$channel]
                    if ($channelCurrentSilent[$channel] -gt
                        $channelLongestSilentAfterActive[$channel]) {
                        $channelLongestSilentAfterActive[$channel] =
                            $channelCurrentSilent[$channel]
                    }
                }
            }
        }
    }

    $summary.audio_metric_windows = if ($summary.frames -eq 0) {
        0
    } else {
        [int][Math]::Ceiling($summary.frames / [double]$audioMetricWindowFrames)
    }
    $summary.audio_metric_active_windows = $audioMetricActiveWindows.Count
    $summary.audio_metric_silent_windows =
        [Math]::Max(0, $summary.audio_metric_windows - $summary.audio_metric_active_windows)
    $summary.audio_metric_active_duty_cycle = if ($summary.audio_metric_windows -gt 0) {
        [Math]::Round(
            $summary.audio_metric_active_windows / [double]$summary.audio_metric_windows, 4)
    } else { 0.0 }
    $summary.audio_metric_mean_peak_abs = if ($summary.frames -gt 0) {
        [Math]::Round($audioMetricPeakSum / [double]$summary.frames, 3)
    } else { 0.0 }
    [int]$audioMetricLongestActive = 0
    [int]$audioMetricLongestSilent = 0
    [int]$audioMetricCurrentActive = 0
    [int]$audioMetricCurrentSilent = 0
    [int]$audioMetricLongestSilentAfterFirstActive = 0
    [bool]$audioMetricSeenActive = $false
    for ($i = 0; $i -lt $summary.audio_metric_windows; ++$i) {
        if ($audioMetricActiveWindows.Contains($i)) {
            $audioMetricSeenActive = $true
            ++$audioMetricCurrentActive
            $audioMetricCurrentSilent = 0
            if ($audioMetricCurrentActive -gt $audioMetricLongestActive) {
                $audioMetricLongestActive = $audioMetricCurrentActive
            }
        } else {
            ++$audioMetricCurrentSilent
            $audioMetricCurrentActive = 0
            if ($audioMetricCurrentSilent -gt $audioMetricLongestSilent) {
                $audioMetricLongestSilent = $audioMetricCurrentSilent
            }
            if ($audioMetricSeenActive -and
                $audioMetricCurrentSilent -gt $audioMetricLongestSilentAfterFirstActive) {
                $audioMetricLongestSilentAfterFirstActive = $audioMetricCurrentSilent
            }
        }
    }
    $summary.audio_metric_longest_active_run_ms =
        $audioMetricLongestActive * $summary.audio_metric_window_ms
    $summary.audio_metric_longest_silent_run_ms =
        $audioMetricLongestSilent * $summary.audio_metric_window_ms
    $summary.audio_metric_longest_silence_after_first_active_ms =
        $audioMetricLongestSilentAfterFirstActive * $summary.audio_metric_window_ms
    $summary.audio_metric_dropout_suspect =
        ($summary.audio_metric_active_windows -gt 0 -and
         $summary.audio_metric_longest_silence_after_first_active_ms -ge 750)

    $channelStats = [System.Collections.Generic.List[object]]::new()
    [int]$activeChannelCount = 0
    for ($channel = 0; $channel -lt 6; ++$channel) {
        $silenceFrames = $channelLongestSilentAfterActive[$channel]
        if ($silenceFrames -gt $summary.adpcma_longest_silence_after_active_frames_max) {
            $summary.adpcma_longest_silence_after_active_frames_max = $silenceFrames
        }
        if ($channelActiveFrames[$channel] -gt 0) {
            ++$activeChannelCount
        }
        $channelStats.Add([pscustomobject]@{
            channel = $channel
            active_frames = $channelActiveFrames[$channel]
            active_duty_cycle = if ($summary.frames -gt 0) {
                [Math]::Round($channelActiveFrames[$channel] / [double]$summary.frames, 4)
            } else { 0.0 }
            longest_active_run_frames = $channelLongestActive[$channel]
            longest_active_run_ms =
                [int][Math]::Round($channelLongestActive[$channel] * 1000.0 / 60.0)
            longest_silence_after_active_frames = $silenceFrames
            longest_silence_after_active_ms =
                [int][Math]::Round($silenceFrames * 1000.0 / 60.0)
            end_events_max = $channelEndEventsMax[$channel]
            rom_underruns_max = $channelRomUnderrunsMax[$channel]
            key_on_writes = $channelKeyOnWrites[$channel]
            key_off_writes = $channelKeyOffWrites[$channel]
            key_on_frames = @($channelKeyOnFrames[$channel])
            first_key_on_frame = if ($channelKeyOnFrames[$channel].Count -gt 0) {
                $channelKeyOnFrames[$channel][0]
            } else { $null }
            last_key_on_frame = if ($channelKeyOnFrames[$channel].Count -gt 0) {
                $channelKeyOnFrames[$channel][$channelKeyOnFrames[$channel].Count - 1]
            } else { $null }
            longest_key_on_gap_frames = $channelLongestKeyOnGap[$channel]
            longest_key_on_gap_ms =
                [int][Math]::Round($channelLongestKeyOnGap[$channel] * 1000.0 / 60.0)
        })
    }
    $summary.adpcma_channel_stats = @($channelStats)
    $summary.adpcma_channel_stats_count = @($channelStats).Count
    $summary.adpcma_active_channel_count = $activeChannelCount
    $summary.adpcma_longest_silence_after_active_ms_max =
        [int][Math]::Round(
            $summary.adpcma_longest_silence_after_active_frames_max * 1000.0 / 60.0)
    $summary.adpcma_channel_dropout_suspect =
        $summary.adpcma_longest_silence_after_active_frames_max -ge 45
    $summary.adpcma_active_masks = @($activeMasks | Sort-Object -Unique)
    $summary.adpcma_key_on_masks = @($adpcmaKeyOnMasks | Sort-Object -Unique)
    $summary.adpcma_key_on_frames = @($adpcmaKeyOnFrames)
    $summary.adpcma_rekey_trace_present = $summary.adpcma_key_on_writes -gt 0
    $summary.adpcmb_control_values = @($adpcmbControlValues | Sort-Object -Unique)
    $summary.adpcmb_control_frames = @($adpcmbControlFrames)
    $summary.adpcmb_start_frames = @($adpcmbStartFrames)
    $summary.adpcm_any_underrun =
        ($summary.adpcma_rom_underruns_max -gt 0 -or
         $summary.adpcmb_rom_underruns_max -gt 0)
    $summary.adpcmb_loop_observed = $summary.adpcmb_loop_events_max -gt 0
    $summary.tc0140syt_command_nmi_pulse_frames = @($tc0140sytCommandNmiFrames)
    $summary.tc0140syt_command_pending_masks =
        @($tc0140sytCommandPendingMasks | Sort-Object -Unique)
    $summary.tc0140syt_command_latches = @($tc0140sytCommandLatches | Sort-Object -Unique)
    $summary.tc0140syt_command_read_frames = @($tc0140sytCommandReadFrames)
    $latencies = [System.Collections.Generic.List[int]]::new()
    foreach ($keyOnFrame in @($adpcmaKeyOnFrames)) {
        [int]$lastCommandReadFrame = -1
        foreach ($commandReadFrame in @($tc0140sytCommandReadFrames)) {
            if ($commandReadFrame -gt $keyOnFrame) {
                break
            }
            $lastCommandReadFrame = [int]$commandReadFrame
        }
        if ($lastCommandReadFrame -ge 0) {
            $latencies.Add([int]$keyOnFrame - $lastCommandReadFrame)
        }
    }
    $summary.tc0140syt_to_adpcma_keyon_latency_observed = $latencies.Count -gt 0
    $summary.tc0140syt_to_adpcma_keyon_latency_frames = @($latencies)
    if ($latencies.Count -gt 0) {
        $summary.tc0140syt_to_adpcma_keyon_latency_min_frames =
            [int](@($latencies) | Measure-Object -Minimum).Minimum
        $summary.tc0140syt_to_adpcma_keyon_latency_max_frames =
            [int](@($latencies) | Measure-Object -Maximum).Maximum
    }
    $summary.z80_nmi_accept_frames = @($z80NmiAcceptFrames)
    return $summary
}

function Get-AudioManifestSummary {
    param([Parameter(Mandatory = $true)][string]$ManifestPath)
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        return [pscustomobject]@{
            present = $false
            chip_count = 0
            stored_sample_count = 0
            register_count = 0
        }
    }

    $json = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    [int]$chipCount = 0
    [int]$storedSampleCount = 0
    [int]$registerCount = 0
    foreach ($chip in @($json.chips)) {
        ++$chipCount
        $storedSampleCount += @($chip.samples).Count
        $registerCount += @($chip.registers).Count
    }

    return [pscustomobject]@{
        present = $true
        chip_count = $chipCount
        stored_sample_count = $storedSampleCount
        register_count = $registerCount
    }
}

function Get-RenderedAudioSummary {
    param([Parameter(Mandatory = $true)][string]$BasePath)
    $summary = New-MissingAudioSummary -BasePath $BasePath
    $manifestSummary = Get-AudioManifestSummary -ManifestPath $summary.manifest
    $summary.manifest_present = [bool]$manifestSummary.present
    $summary.chip_count = [int]$manifestSummary.chip_count
    $summary.stored_sample_count = [int]$manifestSummary.stored_sample_count
    $summary.register_count = [int]$manifestSummary.register_count

    if (-not (Test-Path -LiteralPath $summary.path -PathType Leaf)) {
        return $summary
    }

    $summary.present = $true
    $bytes = [System.IO.File]::ReadAllBytes($summary.path)
    if ($bytes.Length -lt 44 -or
        [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "RIFF" -or
        [System.Text.Encoding]::ASCII.GetString($bytes, 8, 4) -ne "WAVE") {
        $summary.error = "not_riff_wave"
        return $summary
    }

    $fmtOffset = -1
    $fmtSize = 0
    $dataOffset = -1
    $dataSize = 0
    $offset = 12
    while ($offset + 8 -le $bytes.Length) {
        $chunkId = [System.Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $chunkSize = [int](Read-Le32 -Bytes $bytes -Offset ($offset + 4))
        $chunkData = $offset + 8
        if ($chunkData + $chunkSize -gt $bytes.Length) {
            $summary.error = "truncated_chunk"
            return $summary
        }
        if ($chunkId -eq "fmt ") {
            $fmtOffset = $chunkData
            $fmtSize = $chunkSize
        } elseif ($chunkId -eq "data") {
            $dataOffset = $chunkData
            $dataSize = $chunkSize
        }
        $offset = $chunkData + $chunkSize + ($chunkSize % 2)
    }

    if ($fmtOffset -lt 0 -or $fmtSize -lt 16 -or $dataOffset -lt 0) {
        $summary.error = "missing_fmt_or_data"
        return $summary
    }

    $audioFormat = Read-Le16 -Bytes $bytes -Offset $fmtOffset
    $channels = Read-Le16 -Bytes $bytes -Offset ($fmtOffset + 2)
    $sampleRate = [int](Read-Le32 -Bytes $bytes -Offset ($fmtOffset + 4))
    $bitsPerSample = Read-Le16 -Bytes $bytes -Offset ($fmtOffset + 14)
    $summary.sample_rate = $sampleRate
    $summary.channels = $channels
    $summary.bits_per_sample = $bitsPerSample

    if ($audioFormat -ne 1 -or $channels -le 0 -or $bitsPerSample -ne 16) {
        $summary.error = "unsupported_wave_format"
        return $summary
    }

    $frameBytes = $channels * 2
    if ($frameBytes -le 0 -or ($dataSize % $frameBytes) -ne 0) {
        $summary.error = "invalid_data_size"
        return $summary
    }

    $frames = [int]($dataSize / $frameBytes)
    $traceSummary = Get-RenderedAudioTraceSummary -TracePath $summary.trace
    $traceProvidesActivity =
        ([bool]$traceSummary.valid -and [int]$traceSummary.frames -gt 0)
    $windowMs = 100
    $windowFrames = [Math]::Max(1, [int][Math]::Round($sampleRate * $windowMs / 1000.0))
    $windows = if ($frames -eq 0) { 0 } else { [int][Math]::Ceiling($frames / [double]$windowFrames) }
    [int64]$sumAbs = 0
    [int]$peakAbs = 0
    [int]$nonzeroFrames = 0
    [double]$meanAbs = 0.0
    [int]$activeWindows = 0
    [int]$longestActive = 0
    [int]$longestSilent = 0
    [int]$longestSilentAfterFirstActive = 0
    $activeThreshold = 8

    if ($traceProvidesActivity) {
        $nonzeroFrames = [int]$traceSummary.audio_metric_nonzero_frames
        $peakAbs = [int]$traceSummary.peak_abs
        $meanAbs = [double]$traceSummary.audio_metric_mean_peak_abs
        $windowMs = [int]$traceSummary.audio_metric_window_ms
        $windows = [int]$traceSummary.audio_metric_windows
        $activeWindows = [int]$traceSummary.audio_metric_active_windows
        $longestActive = if ($windowMs -gt 0) {
            [int]([int]$traceSummary.audio_metric_longest_active_run_ms / $windowMs)
        } else { 0 }
        $longestSilent = if ($windowMs -gt 0) {
            [int]([int]$traceSummary.audio_metric_longest_silent_run_ms / $windowMs)
        } else { 0 }
        $longestSilentAfterFirstActive = if ($windowMs -gt 0) {
            [int](
                [int]$traceSummary.audio_metric_longest_silence_after_first_active_ms /
                $windowMs)
        } else { 0 }
    } else {
        $activeByWindow = [bool[]]::new($windows)
        for ($frame = 0; $frame -lt $frames; ++$frame) {
            $framePeak = 0
            $base = $dataOffset + ($frame * $frameBytes)
            for ($ch = 0; $ch -lt $channels; ++$ch) {
                $sample = Read-LeS16 -Bytes $bytes -Offset ($base + ($ch * 2))
                $abs = [Math]::Abs($sample)
                $sumAbs += $abs
                if ($abs -gt $framePeak) { $framePeak = $abs }
                if ($abs -gt $peakAbs) { $peakAbs = $abs }
            }
            if ($framePeak -gt 0) {
                ++$nonzeroFrames
            }
            if ($framePeak -gt $activeThreshold -and $windows -gt 0) {
                $windowIndex = [int][Math]::Floor($frame / [double]$windowFrames)
                if ($windowIndex -ge $activeByWindow.Length) {
                    $windowIndex = $activeByWindow.Length - 1
                }
                $activeByWindow[$windowIndex] = $true
            }
        }

        [int]$currentActive = 0
        [int]$currentSilent = 0
        [bool]$seenActive = $false
        for ($i = 0; $i -lt $activeByWindow.Length; ++$i) {
            if ($activeByWindow[$i]) {
                ++$activeWindows
                $seenActive = $true
                ++$currentActive
                $currentSilent = 0
                if ($currentActive -gt $longestActive) { $longestActive = $currentActive }
            } else {
                ++$currentSilent
                $currentActive = 0
                if ($currentSilent -gt $longestSilent) { $longestSilent = $currentSilent }
                if ($seenActive -and $currentSilent -gt $longestSilentAfterFirstActive) {
                    $longestSilentAfterFirstActive = $currentSilent
                }
            }
        }

        $sampleCount = [Math]::Max(1, $frames * $channels)
        $meanAbs = [Math]::Round($sumAbs / [double]$sampleCount, 3)
    }

    $summary.valid = $true
    $summary.frames = $frames
    $summary.duration_ms = if ($sampleRate -gt 0) {
        [int][Math]::Round($frames * 1000.0 / $sampleRate)
    } else { 0 }
    $summary.nonzero_frames = $nonzeroFrames
    $summary.peak_abs = $peakAbs
    $summary.mean_abs = $meanAbs
    $summary.window_ms = $windowMs
    $summary.windows = $windows
    $summary.active_windows = $activeWindows
    $summary.silent_windows = $windows - $activeWindows
    $summary.active_duty_cycle = if ($windows -gt 0) {
        [Math]::Round($activeWindows / [double]$windows, 4)
    } else { 0.0 }
    $summary.longest_active_run_ms = $longestActive * $windowMs
    $summary.longest_silent_run_ms = $longestSilent * $windowMs
    $summary.longest_silence_after_first_active_ms = $longestSilentAfterFirstActive * $windowMs
    $summary.dropout_suspect =
        ($activeWindows -gt 0 -and $summary.longest_silence_after_first_active_ms -ge 750)
    $summary.trace_present = [bool]$traceSummary.present
    $summary.trace_valid = [bool]$traceSummary.valid
    $summary.trace_frames = [int]$traceSummary.frames
    $summary.trace_captured_frames = [int64]$traceSummary.captured_frames
    $summary.trace_sample_rate = [int]$traceSummary.sample_rate
    $summary.trace_peak_abs = [int]$traceSummary.peak_abs
    $summary.adpcma_active_frames = [int]$traceSummary.adpcma_active_frames
    $summary.adpcma_active_masks = @($traceSummary.adpcma_active_masks)
    $summary.adpcma_channel_stats = @($traceSummary.adpcma_channel_stats)
    $summary.adpcma_channel_stats_count = [int]$traceSummary.adpcma_channel_stats_count
    $summary.adpcma_active_channel_count = [int]$traceSummary.adpcma_active_channel_count
    $summary.adpcma_channel_dropout_suspect =
        [bool]$traceSummary.adpcma_channel_dropout_suspect
    $summary.adpcma_longest_silence_after_active_frames_max =
        [int]$traceSummary.adpcma_longest_silence_after_active_frames_max
    $summary.adpcma_longest_silence_after_active_ms_max =
        [int]$traceSummary.adpcma_longest_silence_after_active_ms_max
    $summary.adpcma_key_on_writes = [int]$traceSummary.adpcma_key_on_writes
    $summary.adpcma_key_off_writes = [int]$traceSummary.adpcma_key_off_writes
    $summary.adpcma_key_on_masks = @($traceSummary.adpcma_key_on_masks)
    $summary.adpcma_key_on_frames = @($traceSummary.adpcma_key_on_frames)
    $summary.adpcma_rekey_trace_present = [bool]$traceSummary.adpcma_rekey_trace_present
    $summary.adpcma_end_events_max = [int64]$traceSummary.adpcma_end_events_max
    $summary.adpcma_rom_underruns_max = [int64]$traceSummary.adpcma_rom_underruns_max
    $summary.adpcmb_active_frames = [int]$traceSummary.adpcmb_active_frames
    $summary.adpcmb_control_writes = [int]$traceSummary.adpcmb_control_writes
    $summary.adpcmb_control_values = @($traceSummary.adpcmb_control_values)
    $summary.adpcmb_control_frames = @($traceSummary.adpcmb_control_frames)
    $summary.adpcmb_start_writes = [int]$traceSummary.adpcmb_start_writes
    $summary.adpcmb_repeat_start_writes = [int]$traceSummary.adpcmb_repeat_start_writes
    $summary.adpcmb_start_frames = @($traceSummary.adpcmb_start_frames)
    $summary.adpcmb_end_events_max = [int64]$traceSummary.adpcmb_end_events_max
    $summary.adpcmb_loop_events_max = [int64]$traceSummary.adpcmb_loop_events_max
    $summary.adpcmb_rom_underruns_max = [int64]$traceSummary.adpcmb_rom_underruns_max
    $summary.adpcm_any_underrun = [bool]$traceSummary.adpcm_any_underrun
    $summary.adpcmb_loop_observed = [bool]$traceSummary.adpcmb_loop_observed
    $summary.tc0140syt_present = [bool]$traceSummary.tc0140syt_present
    $summary.tc0140syt_command_nmi_pulses_max =
        [int64]$traceSummary.tc0140syt_command_nmi_pulses_max
    $summary.tc0140syt_command_nmi_pulse_count =
        [int]$traceSummary.tc0140syt_command_nmi_pulse_count
    $summary.tc0140syt_command_nmi_pulse_frames =
        @($traceSummary.tc0140syt_command_nmi_pulse_frames)
    $summary.tc0140syt_command_pending_frames =
        [int]$traceSummary.tc0140syt_command_pending_frames
    $summary.tc0140syt_command_pending_masks =
        @($traceSummary.tc0140syt_command_pending_masks)
    $summary.tc0140syt_command_latches = @($traceSummary.tc0140syt_command_latches)
    $summary.tc0140syt_command_write_count_max =
        [int64]$traceSummary.tc0140syt_command_write_count_max
    $summary.tc0140syt_command_read_count_max =
        [int64]$traceSummary.tc0140syt_command_read_count_max
    $summary.tc0140syt_command_read_frames =
        @($traceSummary.tc0140syt_command_read_frames)
    $summary.tc0140syt_to_adpcma_keyon_latency_observed =
        [bool]$traceSummary.tc0140syt_to_adpcma_keyon_latency_observed
    $summary.tc0140syt_to_adpcma_keyon_latency_min_frames =
        [int]$traceSummary.tc0140syt_to_adpcma_keyon_latency_min_frames
    $summary.tc0140syt_to_adpcma_keyon_latency_max_frames =
        [int]$traceSummary.tc0140syt_to_adpcma_keyon_latency_max_frames
    $summary.tc0140syt_to_adpcma_keyon_latency_frames =
        @($traceSummary.tc0140syt_to_adpcma_keyon_latency_frames)
    $summary.tc0140syt_reply_write_count_max =
        [int64]$traceSummary.tc0140syt_reply_write_count_max
    $summary.tc0140syt_reply_read_count_max =
        [int64]$traceSummary.tc0140syt_reply_read_count_max
    $summary.tc0140syt_clear_count_max =
        [int64]$traceSummary.tc0140syt_clear_count_max
    $summary.z80_sound_cpu_present = [bool]$traceSummary.z80_sound_cpu_present
    $summary.z80_nmi_accept_count_max =
        [int64]$traceSummary.z80_nmi_accept_count_max
    $summary.z80_nmi_accept_frames = @($traceSummary.z80_nmi_accept_frames)
    $summary.z80_irq_accept_count_max =
        [int64]$traceSummary.z80_irq_accept_count_max
    return $summary
}

function Get-ScreenshotSidecarPath {
    param(
        [Parameter(Mandatory = $true)][string]$ScreenshotPath,
        [Parameter(Mandatory = $true)][string]$ViewName
    )
    $dir = Split-Path -Parent $ScreenshotPath
    $leaf = Split-Path -Leaf $ScreenshotPath
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        return $null
    }
    $suffix = ".$ViewName.bin"
    $match = Get-ChildItem -LiteralPath $dir -File |
        Where-Object {
            $_.Name.StartsWith("$leaf.", [System.StringComparison]::Ordinal) -and
            $_.Name.EndsWith($suffix, [System.StringComparison]::Ordinal)
        } |
        Select-Object -First 1
    if ($null -eq $match) {
        return $null
    }
    return $match.FullName
}

function New-SourceCountObject {
    param([Parameter(Mandatory = $true)][long[]]$Counts)
    $names = Get-PrioritySourceNames
    $out = [ordered]@{}
    for ($i = 0; $i -lt $Counts.Count; ++$i) {
        if ($Counts[$i] -ne 0) {
            $out[$names[$i]] = $Counts[$i]
        }
    }
    return [pscustomobject]$out
}

function Get-PrioritySourceNames {
    return @(
        "backdrop",
        "tc0100scn_bg0",
        "tc0100scn_bg1",
        "tc0100scn_text",
        "roz",
        "sprite",
        "tc0480scp_bg0",
        "tc0480scp_bg1",
        "tc0480scp_bg2",
        "tc0480scp_bg3",
        "tc0480scp_text",
        "tc0100scn_secondary_bg0",
        "tc0100scn_secondary_bg1",
        "tc0100scn_secondary_text",
        "source14",
        "source15"
    )
}

function New-EmptyBounds {
    return [pscustomobject]@{
        seen = $false
        pixels = [long]0
        x_min = 0
        y_min = 0
        x_max = 0
        y_max = 0
    }
}

function New-SourceBoundsArray {
    param([Parameter(Mandatory = $true)][int]$Count)
    $bounds = New-Object 'System.Object[]' $Count
    for ($i = 0; $i -lt $Count; ++$i) {
        $bounds[$i] = New-EmptyBounds
    }
    return ,$bounds
}

function Add-SourceBounds {
    param(
        [Parameter(Mandatory = $true)][object[]]$Bounds,
        [Parameter(Mandatory = $true)][int]$Source,
        [Parameter(Mandatory = $true)][int]$PixelIndex
    )
    if ($Source -lt 0 -or $Source -ge $Bounds.Count) {
        return
    }
    $x = $PixelIndex % 320
    $y = [int]($PixelIndex / 320)
    $b = $Bounds[$Source]
    if (-not $b.seen) {
        $b.seen = $true
        $b.x_min = $x
        $b.y_min = $y
        $b.x_max = $x
        $b.y_max = $y
    } else {
        if ($x -lt $b.x_min) { $b.x_min = $x }
        if ($y -lt $b.y_min) { $b.y_min = $y }
        if ($x -gt $b.x_max) { $b.x_max = $x }
        if ($y -gt $b.y_max) { $b.y_max = $y }
    }
    $b.pixels = [long]($b.pixels + 1)
}

function New-SourceBoundsObject {
    param([Parameter(Mandatory = $true)][object[]]$Bounds)
    $names = Get-PrioritySourceNames
    $out = [ordered]@{}
    for ($i = 0; $i -lt $Bounds.Count; ++$i) {
        $b = $Bounds[$i]
        if ($b.seen) {
            $out[$names[$i]] = [pscustomobject]@{
                pixels = [long]$b.pixels
                x_min = [int]$b.x_min
                y_min = [int]$b.y_min
                x_max = [int]$b.x_max
                y_max = [int]$b.y_max
            }
        }
    }
    return [pscustomobject]$out
}

function New-BoundsSummary {
    param([Parameter(Mandatory = $true)]$Bounds)
    if (-not $Bounds.seen) {
        return [pscustomobject]@{
            present = $false
            pixels = 0
        }
    }
    return [pscustomobject]@{
        present = $true
        pixels = [long]$Bounds.pixels
        x_min = [int]$Bounds.x_min
        y_min = [int]$Bounds.y_min
        x_max = [int]$Bounds.x_max
        y_max = [int]$Bounds.y_max
    }
}

function Get-PriorityDecisionSummary {
    param([Parameter(Mandatory = $true)][string]$ScreenshotPath)
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "priority_decisions_v1"
    if ($null -eq $path) {
        return [pscustomobject]@{
            present = $false
            path = $null
            bytes = 0
            valid = $false
            records = 0
            final_sources = [pscustomobject]@{}
            attempted_sources = [pscustomobject]@{}
            rejected_sources = [pscustomobject]@{}
            last_rejected_sources = [pscustomobject]@{}
            final_source_bounds = [pscustomobject]@{}
            attempted_source_bounds = [pscustomobject]@{}
            rejected_source_bounds = [pscustomobject]@{}
            non_backdrop_bounds = [pscustomobject]@{ present = $false; pixels = 0 }
            sprite_occupied_pixels = 0
            sprite_priority_reject_pixels = 0
            sprite_occupancy_reject_pixels = 0
            layer_priority_reject_pixels = 0
        }
    }

    $bytes = [System.IO.File]::ReadAllBytes($path)
    $recordBytes = 12
    $valid = ($bytes.Length -gt 0 -and ($bytes.Length % $recordBytes) -eq 0)
    $records = if ($valid) { [int]($bytes.Length / $recordBytes) } else { 0 }
    $finalCounts = [long[]]::new(16)
    $attemptedCounts = [long[]]::new(16)
    $rejectedCounts = [long[]]::new(16)
    $lastRejectedCounts = [long[]]::new(16)
    $finalBounds = New-SourceBoundsArray -Count 16
    $attemptedBounds = New-SourceBoundsArray -Count 16
    $rejectedBounds = New-SourceBoundsArray -Count 16
    $nonBackdropBounds = New-EmptyBounds
    [long]$spriteOccupied = 0
    [long]$spritePriorityReject = 0
    [long]$spriteOccupancyReject = 0
    [long]$layerPriorityReject = 0

    for ($i = 0; $i -lt $records; ++$i) {
        $off = $i * $recordBytes
        $finalSource = [int]$bytes[$off]
        if ($finalSource -ge 0 -and $finalSource -lt $finalCounts.Count) {
            $finalCounts[$finalSource] += 1
            Add-SourceBounds -Bounds $finalBounds -Source $finalSource -PixelIndex $i
            if ($finalSource -ne 0) {
                Add-SourceBounds -Bounds @($nonBackdropBounds) -Source 0 -PixelIndex $i
            }
        }
        $flags = [int]$bytes[$off + 3]
        if (($flags -band 0x01) -ne 0) { $spriteOccupied += 1 }
        if (($flags -band 0x02) -ne 0) { $spritePriorityReject += 1 }
        if (($flags -band 0x04) -ne 0) { $spriteOccupancyReject += 1 }
        if (($flags -band 0x08) -ne 0) { $layerPriorityReject += 1 }

        $attemptedMask = [int]$bytes[$off + 4] -bor ([int]$bytes[$off + 5] -shl 8)
        for ($bit = 0; $bit -lt $attemptedCounts.Count; ++$bit) {
            if (($attemptedMask -band (1 -shl $bit)) -ne 0) {
                $attemptedCounts[$bit] += 1
                Add-SourceBounds -Bounds $attemptedBounds -Source $bit -PixelIndex $i
            }
        }

        $rejectedMask = [int]$bytes[$off + 6] -bor ([int]$bytes[$off + 7] -shl 8)
        for ($bit = 0; $bit -lt $rejectedCounts.Count; ++$bit) {
            if (($rejectedMask -band (1 -shl $bit)) -ne 0) {
                $rejectedCounts[$bit] += 1
                Add-SourceBounds -Bounds $rejectedBounds -Source $bit -PixelIndex $i
            }
        }

        $lastRejected = [int]$bytes[$off + 10]
        if ($rejectedMask -ne 0 -and $lastRejected -ge 0 -and
            $lastRejected -lt $lastRejectedCounts.Count) {
            $lastRejectedCounts[$lastRejected] += 1
        }
    }

    return [pscustomobject]@{
        present = $true
        path = $path
        bytes = $bytes.Length
        valid = $valid
        records = $records
        final_sources = New-SourceCountObject -Counts $finalCounts
        attempted_sources = New-SourceCountObject -Counts $attemptedCounts
        rejected_sources = New-SourceCountObject -Counts $rejectedCounts
        last_rejected_sources = New-SourceCountObject -Counts $lastRejectedCounts
        final_source_bounds = New-SourceBoundsObject -Bounds $finalBounds
        attempted_source_bounds = New-SourceBoundsObject -Bounds $attemptedBounds
        rejected_source_bounds = New-SourceBoundsObject -Bounds $rejectedBounds
        non_backdrop_bounds = New-BoundsSummary -Bounds $nonBackdropBounds
        sprite_occupied_pixels = $spriteOccupied
        sprite_priority_reject_pixels = $spritePriorityReject
        sprite_occupancy_reject_pixels = $spriteOccupancyReject
        layer_priority_reject_pixels = $layerPriorityReject
    }
}

function Get-DecodedSpriteObjectSummary {
    param([Parameter(Mandatory = $true)][string]$ScreenshotPath)
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "decoded_sprite_objects_v1"
    if ($null -eq $path) {
        return [pscustomobject]@{
            present = $false
            path = $null
            bytes = 0
            valid = $false
            records = 0
        }
    }

    $length = (Get-Item -LiteralPath $path).Length
    $recordBytes = 40
    return [pscustomobject]@{
        present = $true
        path = $path
        bytes = $length
        valid = ($length -ge 0 -and ($length % $recordBytes) -eq 0)
        records = [int]($length / $recordBytes)
    }
}

function Get-SpriteControlStateSummary {
    param([string]$ScreenshotPath)
    $path = $null
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath `
            -ViewName "sprite_control_state_v1"
    }
    if ($null -eq $path) {
        return [pscustomobject]@{
            present = $false
            path = $null
            bytes = 0
            valid = $false
            version = 0
            mode = 0
            active_area_source = 0
            buffer_policy = 0
            active_area = 0
            disabled = $false
            flip_screen = $false
            buffer_valid = $false
            reset_disable_each_list = $false
            master_scroll_x = 0
            master_scroll_y = 0
            control_marker_count = 0
            disable_marker_count = 0
            flip_marker_count = 0
            active_area_switch_count = 0
            master_scroll_marker_count = 0
            extra_scroll_marker_count = 0
            blank_record_count = 0
        }
    }

    $bytes = [System.IO.File]::ReadAllBytes($path)
    $valid = ($bytes.Length -ge 64 -and [int]$bytes[0] -eq 1)
    return [pscustomobject]@{
        present = $true
        path = $path
        bytes = $bytes.Length
        valid = $valid
        version = if ($bytes.Length -ge 1) { [int]$bytes[0] } else { 0 }
        mode = if ($bytes.Length -ge 2) { [int]$bytes[1] } else { 0 }
        active_area_source = if ($bytes.Length -ge 3) { [int]$bytes[2] } else { 0 }
        buffer_policy = if ($bytes.Length -ge 4) { [int]$bytes[3] } else { 0 }
        active_area = if ($bytes.Length -ge 8) { Read-Le32 -Bytes $bytes -Offset 4 } else { 0 }
        disabled = ($bytes.Length -ge 9 -and [int]$bytes[8] -ne 0)
        flip_screen = ($bytes.Length -ge 10 -and [int]$bytes[9] -ne 0)
        buffer_valid = ($bytes.Length -ge 11 -and [int]$bytes[10] -ne 0)
        reset_disable_each_list = ($bytes.Length -ge 12 -and [int]$bytes[11] -ne 0)
        master_scroll_x = if ($bytes.Length -ge 14) { Read-LeS16 -Bytes $bytes -Offset 12 } else { 0 }
        master_scroll_y = if ($bytes.Length -ge 16) { Read-LeS16 -Bytes $bytes -Offset 14 } else { 0 }
        control_marker_count = if ($bytes.Length -ge 20) { Read-Le32 -Bytes $bytes -Offset 16 } else { 0 }
        disable_marker_count = if ($bytes.Length -ge 24) { Read-Le32 -Bytes $bytes -Offset 20 } else { 0 }
        flip_marker_count = if ($bytes.Length -ge 28) { Read-Le32 -Bytes $bytes -Offset 24 } else { 0 }
        active_area_switch_count = if ($bytes.Length -ge 32) { Read-Le32 -Bytes $bytes -Offset 28 } else { 0 }
        master_scroll_marker_count = if ($bytes.Length -ge 36) { Read-Le32 -Bytes $bytes -Offset 32 } else { 0 }
        extra_scroll_marker_count = if ($bytes.Length -ge 40) { Read-Le32 -Bytes $bytes -Offset 36 } else { 0 }
        blank_record_count = if ($bytes.Length -ge 44) { Read-Le32 -Bytes $bytes -Offset 40 } else { 0 }
    }
}

function Get-SpriteBufferStateSummary {
    param([string]$ScreenshotPath)
    $path = $null
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath `
            -ViewName "sprite_buffer_state_v1"
    }
    if ($null -eq $path) {
        return [pscustomobject]@{
            present = $false
            path = $null
            bytes = 0
            valid = $false
            version = 0
            policy = 0
            buffer_valid = $false
            source_ram_present = $false
            overlay_word_mask = 0
            overlay_word_count = 0
            words_per_entry = 0
            delayed_source_used = $false
            current_copy_to_latched = $false
            current_copy_to_delay = $false
            partial_overlay_profile = $false
            source_ram_bytes = 0
            latched_buffer_bytes = 0
            delay_buffer_bytes = 0
            overlay_entry_count = 0
            overlay_word_total = 0
            current_to_latched_bytes = 0
            current_to_delay_bytes = 0
            delayed_source_bytes = 0
        }
    }

    $bytes = [System.IO.File]::ReadAllBytes($path)
    $valid = ($bytes.Length -ge 64 -and [int]$bytes[0] -eq 1)
    return [pscustomobject]@{
        present = $true
        path = $path
        bytes = $bytes.Length
        valid = $valid
        version = if ($bytes.Length -ge 1) { [int]$bytes[0] } else { 0 }
        policy = if ($bytes.Length -ge 2) { [int]$bytes[1] } else { 0 }
        buffer_valid = ($bytes.Length -ge 3 -and [int]$bytes[2] -ne 0)
        source_ram_present = ($bytes.Length -ge 4 -and [int]$bytes[3] -ne 0)
        overlay_word_mask = if ($bytes.Length -ge 6) { Read-Le16 -Bytes $bytes -Offset 4 } else { 0 }
        overlay_word_count = if ($bytes.Length -ge 8) { Read-Le16 -Bytes $bytes -Offset 6 } else { 0 }
        words_per_entry = if ($bytes.Length -ge 10) { Read-Le16 -Bytes $bytes -Offset 8 } else { 0 }
        delayed_source_used = ($bytes.Length -ge 11 -and [int]$bytes[10] -ne 0)
        current_copy_to_latched = ($bytes.Length -ge 12 -and [int]$bytes[11] -ne 0)
        current_copy_to_delay = ($bytes.Length -ge 13 -and [int]$bytes[12] -ne 0)
        partial_overlay_profile = ($bytes.Length -ge 14 -and [int]$bytes[13] -ne 0)
        source_ram_bytes = if ($bytes.Length -ge 20) { Read-Le32 -Bytes $bytes -Offset 16 } else { 0 }
        latched_buffer_bytes = if ($bytes.Length -ge 24) { Read-Le32 -Bytes $bytes -Offset 20 } else { 0 }
        delay_buffer_bytes = if ($bytes.Length -ge 28) { Read-Le32 -Bytes $bytes -Offset 24 } else { 0 }
        overlay_entry_count = if ($bytes.Length -ge 32) { Read-Le32 -Bytes $bytes -Offset 28 } else { 0 }
        overlay_word_total = if ($bytes.Length -ge 36) { Read-Le32 -Bytes $bytes -Offset 32 } else { 0 }
        current_to_latched_bytes = if ($bytes.Length -ge 40) { Read-Le32 -Bytes $bytes -Offset 36 } else { 0 }
        current_to_delay_bytes = if ($bytes.Length -ge 44) { Read-Le32 -Bytes $bytes -Offset 40 } else { 0 }
        delayed_source_bytes = if ($bytes.Length -ge 48) { Read-Le32 -Bytes $bytes -Offset 44 } else { 0 }
    }
}

function Get-SourceCount {
    param(
        [Parameter(Mandatory = $true)]$Counts,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if ($null -eq $Counts) {
        return [long]0
    }
    $property = $Counts.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return [long]0
    }
    return [long]$property.Value
}

function Test-ScreenshotSidecarPresent {
    param(
        [string]$ScreenshotPath,
        [Parameter(Mandatory = $true)][string]$ViewName
    )
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $false
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName $ViewName
    return $null -ne $path -and (Test-Path -LiteralPath $path -PathType Leaf) -and
        (Get-Item -LiteralPath $path).Length -gt 0
}

function Get-IrqStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        configured_vblank_irq_level = 0
        configured_sprite_dma_irq_level = 0
        last_vblank_irq_level = 0
        last_irq_ack_level = 0
        vblank_irq_raised = [int64]0
        vblank_irq_acked = [int64]0
        last_sprite_dma_irq_level = 0
        last_sprite_dma_irq_ack_level = 0
        sprite_dma_irq_raised = [int64]0
        sprite_dma_irq_acked = [int64]0
        vblank_irq_asserted = $false
        irq_ack_seen = $false
        sprite_dma_irq_asserted = $false
        sprite_dma_irq_ack_seen = $false
        vblank_irq_pending = $false
        sprite_dma_irq_pending = $false
        main_irq_line_level = 0
        irq2_placeholder_seen = $false
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "irq_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 24) {
        $empty.present = $true
        return $empty
    }
    $raised = [System.BitConverter]::ToUInt64($bytes, 4)
    $acked = [System.BitConverter]::ToUInt64($bytes, 12)
    $configuredVblank = [int]$bytes[0]
    $lastVblank = [int]$bytes[2]
    $lastAck = [int]$bytes[3]
    $lastSpriteDma = if ($bytes.Length -ge 24) { [int]$bytes[22] } else { 0 }
    $lastSpriteDmaAck = if ($bytes.Length -ge 24) { [int]$bytes[23] } else { 0 }
    $spriteRaised = if ($bytes.Length -ge 48) {
        [System.BitConverter]::ToUInt64($bytes, 24)
    } else {
        [uint64]0
    }
    $spriteAcked = if ($bytes.Length -ge 48) {
        [System.BitConverter]::ToUInt64($bytes, 32)
    } else {
        [uint64]0
    }
    $spriteAssertedFlag = $bytes.Length -ge 41 -and [int]$bytes[40] -ne 0
    $spriteAckFlag = $bytes.Length -ge 42 -and [int]$bytes[41] -ne 0
    $vblankPending = $bytes.Length -ge 43 -and [int]$bytes[42] -ne 0
    $spritePending = $bytes.Length -ge 44 -and [int]$bytes[43] -ne 0
    $mainIrqLine = if ($bytes.Length -ge 45) { [int]$bytes[44] } else { 0 }
    return [pscustomobject]@{
        present = $true
        valid = $true
        configured_vblank_irq_level = $configuredVblank
        configured_sprite_dma_irq_level = [int]$bytes[1]
        last_vblank_irq_level = $lastVblank
        last_irq_ack_level = $lastAck
        vblank_irq_raised = [int64]$raised
        vblank_irq_acked = [int64]$acked
        last_sprite_dma_irq_level = $lastSpriteDma
        last_sprite_dma_irq_ack_level = $lastSpriteDmaAck
        sprite_dma_irq_raised = [int64]$spriteRaised
        sprite_dma_irq_acked = [int64]$spriteAcked
        vblank_irq_asserted = ($raised -gt 0 -or [int]$bytes[20] -ne 0)
        irq_ack_seen = ($acked -gt 0 -or [int]$bytes[21] -ne 0)
        sprite_dma_irq_asserted = ($spriteRaised -gt 0 -or $spriteAssertedFlag)
        sprite_dma_irq_ack_seen = ($spriteAcked -gt 0 -or $spriteAckFlag)
        vblank_irq_pending = $vblankPending
        sprite_dma_irq_pending = $spritePending
        main_irq_line_level = $mainIrqLine
        irq2_placeholder_seen =
            ($configuredVblank -eq 2 -or [int]$bytes[1] -eq 2 -or
             $lastVblank -eq 2 -or $lastAck -eq 2 -or
             $lastSpriteDma -eq 2 -or $lastSpriteDmaAck -eq 2 -or
             $mainIrqLine -eq 2)
    }
}

function Get-SoundBankStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        bank = 0
        page = 0
        page_count = 0
        page_valid = $false
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "sound_bank_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 4) {
        $empty.present = $true
        return $empty
    }
    return [pscustomobject]@{
        present = $true
        valid = $true
        bank = [int]$bytes[0]
        page = [int]$bytes[1]
        page_count = [int]$bytes[2]
        page_valid = ([int]$bytes[3] -ne 0)
    }
}

function Get-SoundResetStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        version = 0
        reset_held = $false
        last_value = 0
        write_seen = $false
        write_count = 0
        assert_count = 0
        release_count = 0
        last_address = 0
        z80_pc = 0
        z80_reset_line = $false
        tc0140syt_profile = $false
        sound_rom_present = $false
        main_port = 0
        sound_port = 0
        bank_page = 0
        z80_nmi_accept_count = 0
        z80_irq_accept_count = 0
        status = 0
        command_pending_mask = 0
        reply_pending_mask = 0
        bank_page_valid = $false
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "sound_reset_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 40) {
        $empty.present = $true
        return $empty
    }
    return [pscustomobject]@{
        present = $true
        valid = ([int]$bytes[0] -eq 1)
        version = [int]$bytes[0]
        reset_held = ([int]$bytes[1] -ne 0)
        last_value = [int]$bytes[2]
        write_seen = ([int]$bytes[3] -ne 0)
        write_count = [int][System.BitConverter]::ToUInt32($bytes, 4)
        assert_count = [int][System.BitConverter]::ToUInt32($bytes, 8)
        release_count = [int][System.BitConverter]::ToUInt32($bytes, 12)
        last_address = [int][System.BitConverter]::ToUInt32($bytes, 16)
        z80_pc = [int][System.BitConverter]::ToUInt16($bytes, 20)
        z80_reset_line = ([int]$bytes[22] -ne 0)
        tc0140syt_profile = ([int]$bytes[23] -ne 0)
        sound_rom_present = ([int]$bytes[24] -ne 0)
        main_port = [int]$bytes[25]
        sound_port = [int]$bytes[26]
        bank_page = [int]$bytes[27]
        z80_nmi_accept_count = [int][System.BitConverter]::ToUInt32($bytes, 28)
        z80_irq_accept_count = [int][System.BitConverter]::ToUInt32($bytes, 32)
        status = [int]$bytes[36]
        command_pending_mask = [int]$bytes[37]
        reply_pending_mask = [int]$bytes[38]
        bank_page_valid = ([int]$bytes[39] -ne 0)
    }
}

function Get-WatchdogStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        version = 0
        write_seen = $false
        confirmed_seen = $false
        suspect_seen = $false
        write_count = 0
        confirmed_write_count = 0
        suspect_write_count = 0
        last_address = 0
        last_value = 0
        last_window = 0
        address_map = 0
        io_profile = 0
        confirmed_base = 0
        suspect_base = 0
        confirmed_window_present = $false
        suspect_window_present = $false
        timeout_model_supported = $false
        reset_model_supported = $false
        priority_suspect_address = 0
        last_write_confirmed = $false
        write_window_size = 0
        real_map = $false
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "watchdog_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 48) {
        $empty.present = $true
        return $empty
    }
    return [pscustomobject]@{
        present = $true
        valid = ([int]$bytes[0] -eq 1)
        version = [int]$bytes[0]
        write_seen = ([int]$bytes[1] -ne 0)
        confirmed_seen = ([int]$bytes[2] -ne 0)
        suspect_seen = ([int]$bytes[3] -ne 0)
        write_count = [int][System.BitConverter]::ToUInt32($bytes, 4)
        confirmed_write_count = [int][System.BitConverter]::ToUInt32($bytes, 8)
        suspect_write_count = [int][System.BitConverter]::ToUInt32($bytes, 12)
        last_address = [int][System.BitConverter]::ToUInt32($bytes, 16)
        last_value = [int]$bytes[20]
        last_window = [int]$bytes[21]
        address_map = [int]$bytes[22]
        io_profile = [int]$bytes[23]
        confirmed_base = [int][System.BitConverter]::ToUInt32($bytes, 24)
        suspect_base = [int][System.BitConverter]::ToUInt32($bytes, 28)
        confirmed_window_present = ([int]$bytes[32] -ne 0)
        suspect_window_present = ([int]$bytes[33] -ne 0)
        timeout_model_supported = ([int]$bytes[34] -ne 0)
        reset_model_supported = ([int]$bytes[35] -ne 0)
        priority_suspect_address = [int][System.BitConverter]::ToUInt32($bytes, 36)
        last_write_confirmed = ([int]$bytes[44] -ne 0)
        write_window_size = [int]$bytes[46]
        real_map = ([int]$bytes[47] -ne 0)
    }
}

function Get-MainBusStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        version = 0
        observer_installed = $false
        last_write = $false
        last_mapped = $false
        read_count = 0
        write_count = 0
        open_bus_read_count = 0
        unmapped_write_count = 0
        odd_access_count = 0
        inferred_word_pair_count = 0
        last_address = 0
        last_value = 0
        last_open_bus = $false
        previous_valid = $false
        last_pair_inferred = $false
        address_map = 0
        real_map = $false
        byte_observer_supported = $false
        wait_state_model_supported = $false
        previous_address = 0
        previous_value = 0
        previous_write = $false
        previous_mapped = $false
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "main_bus_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 64) {
        $empty.present = $true
        return $empty
    }
    return [pscustomobject]@{
        present = $true
        valid = ([int]$bytes[0] -eq 1)
        version = [int]$bytes[0]
        observer_installed = ([int]$bytes[1] -ne 0)
        last_write = ([int]$bytes[2] -ne 0)
        last_mapped = ([int]$bytes[3] -ne 0)
        read_count = [int][System.BitConverter]::ToUInt32($bytes, 4)
        write_count = [int][System.BitConverter]::ToUInt32($bytes, 8)
        open_bus_read_count = [int][System.BitConverter]::ToUInt32($bytes, 12)
        unmapped_write_count = [int][System.BitConverter]::ToUInt32($bytes, 16)
        odd_access_count = [int][System.BitConverter]::ToUInt32($bytes, 20)
        inferred_word_pair_count = [int][System.BitConverter]::ToUInt32($bytes, 24)
        last_address = [int][System.BitConverter]::ToUInt32($bytes, 28)
        last_value = [int]$bytes[32]
        last_open_bus = ([int]$bytes[33] -ne 0)
        previous_valid = ([int]$bytes[34] -ne 0)
        last_pair_inferred = ([int]$bytes[35] -ne 0)
        address_map = [int]$bytes[36]
        real_map = ([int]$bytes[37] -ne 0)
        byte_observer_supported = ([int]$bytes[38] -ne 0)
        wait_state_model_supported = ([int]$bytes[39] -ne 0)
        previous_address = [int][System.BitConverter]::ToUInt32($bytes, 40)
        previous_value = [int]$bytes[44]
        previous_write = ([int]$bytes[45] -ne 0)
        previous_mapped = ([int]$bytes[46] -ne 0)
    }
}

function Get-IoOutputStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        write_seen = $false
        last_offset = 0
        last_value = 0
        latch = 0
        coin_line_mask = 0
        coin_lockout_mask = 0
        write_count = 0
        coin_counter_0 = 0
        coin_counter_1 = 0
        coin_counter_2 = 0
        coin_counter_3 = 0
        coin_counter_edges = 0
        coin_counter_slots = 0
        coin_lockout_slots = 0
        cabinet_test_mask = 0
        four_player_service_mask = 0
        raw_input_system = 0
        raw_input_coin_extension = 0
        split_panel_input = 0
        last_address = 0
        address_map = 0
        io_profile = 0
        aux_profile = 0
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "io_output_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 24) {
        $empty.present = $true
        return $empty
    }
    $coin0 = [int][System.BitConverter]::ToUInt32($bytes, 10)
    $coin1 = [int][System.BitConverter]::ToUInt32($bytes, 14)
    [int]$coin2 = 0
    [int]$coin3 = 0
    [int]$coinCounterSlots = 2
    [int]$coinLockoutSlots = 4
    [int]$lastAddress = ([int]$bytes[18] -bor ([int]$bytes[19] -shl 8) -bor
                         ([int]$bytes[20] -shl 16))
    [int]$addressMap = [int]$bytes[21]
    [int]$ioProfile = [int]$bytes[22]
    [int]$auxProfile = [int]$bytes[23]
    [int]$cabinetTestMask = 0
    [int]$fourPlayerServiceMask = 0
    [int]$rawInputSystem = 0
    [int]$rawInputCoinExtension = 0
    [int]$splitPanelInput = 0
    if ($bytes.Length -ge 40) {
        $coin2 = [int][System.BitConverter]::ToUInt32($bytes, 18)
        $coin3 = [int][System.BitConverter]::ToUInt32($bytes, 22)
        $coinCounterSlots = [int]$bytes[26]
        $coinLockoutSlots = [int]$bytes[27]
        $lastAddress = [int][System.BitConverter]::ToUInt32($bytes, 28)
        $addressMap = [int]$bytes[32]
        $ioProfile = [int]$bytes[33]
        $auxProfile = [int]$bytes[34]
        $cabinetTestMask = [int]$bytes[35]
        $fourPlayerServiceMask = [int]$bytes[36]
        $rawInputSystem = [int]$bytes[37]
        $rawInputCoinExtension = [int]$bytes[38]
        $splitPanelInput = [int]$bytes[39]
    }
    return [pscustomobject]@{
        present = $true
        valid = $true
        write_seen = ([int]$bytes[0] -ne 0)
        last_offset = [int]$bytes[1]
        last_value = [int]$bytes[2]
        latch = [int]$bytes[3]
        coin_line_mask = [int]$bytes[4]
        coin_lockout_mask = [int]$bytes[5]
        write_count = [int][System.BitConverter]::ToUInt32($bytes, 6)
        coin_counter_0 = $coin0
        coin_counter_1 = $coin1
        coin_counter_2 = $coin2
        coin_counter_3 = $coin3
        coin_counter_edges = ($coin0 + $coin1 + $coin2 + $coin3)
        coin_counter_slots = $coinCounterSlots
        coin_lockout_slots = $coinLockoutSlots
        cabinet_test_mask = $cabinetTestMask
        four_player_service_mask = $fourPlayerServiceMask
        raw_input_system = $rawInputSystem
        raw_input_coin_extension = $rawInputCoinExtension
        split_panel_input = $splitPanelInput
        last_address = $lastAddress
        address_map = $addressMap
        io_profile = $ioProfile
        aux_profile = $auxProfile
    }
}

function Get-IoAccessStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        version = 0
        access_seen = $false
        last_write = $false
        last_window = 0
        input_read_count = 0
        output_write_count = 0
        dip_read_count = 0
        service_read_count = 0
        read_even_count = 0
        read_odd_count = 0
        write_even_count = 0
        write_odd_count = 0
        inferred_read_pair_count = 0
        inferred_write_pair_count = 0
        last_address = 0
        last_value = 0
        last_pair_inferred = $false
        address_map = 0
        io_profile = 0
        input_profile = 0
        aux_profile = 0
        raw_input_system = 0
        raw_input_coin_extension = 0
        cabinet_test_mask = 0
        four_player_service_mask = 0
        split_panel_input = $false
        previous_valid = $false
        previous_address = 0
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "io_access_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 64) {
        $empty.present = $true
        return $empty
    }
    return [pscustomobject]@{
        present = $true
        valid = ([int]$bytes[0] -eq 1)
        version = [int]$bytes[0]
        access_seen = ([int]$bytes[1] -ne 0)
        last_write = ([int]$bytes[2] -ne 0)
        last_window = [int]$bytes[3]
        input_read_count = [int][System.BitConverter]::ToUInt32($bytes, 4)
        output_write_count = [int][System.BitConverter]::ToUInt32($bytes, 8)
        dip_read_count = [int][System.BitConverter]::ToUInt32($bytes, 12)
        service_read_count = [int][System.BitConverter]::ToUInt32($bytes, 16)
        read_even_count = [int][System.BitConverter]::ToUInt32($bytes, 20)
        read_odd_count = [int][System.BitConverter]::ToUInt32($bytes, 24)
        write_even_count = [int][System.BitConverter]::ToUInt32($bytes, 28)
        write_odd_count = [int][System.BitConverter]::ToUInt32($bytes, 32)
        inferred_read_pair_count = [int][System.BitConverter]::ToUInt32($bytes, 36)
        inferred_write_pair_count = [int][System.BitConverter]::ToUInt32($bytes, 40)
        last_address = [int][System.BitConverter]::ToUInt32($bytes, 44)
        last_value = [int]$bytes[48]
        last_pair_inferred = ([int]$bytes[49] -ne 0)
        address_map = [int]$bytes[50]
        io_profile = [int]$bytes[51]
        input_profile = [int]$bytes[52]
        aux_profile = [int]$bytes[53]
        raw_input_system = [int]$bytes[54]
        raw_input_coin_extension = [int]$bytes[55]
        cabinet_test_mask = [int]$bytes[56]
        four_player_service_mask = [int]$bytes[57]
        split_panel_input = ([int]$bytes[58] -ne 0)
        previous_valid = ([int]$bytes[59] -ne 0)
        previous_address = [int][System.BitConverter]::ToUInt32($bytes, 60)
    }
}

function Get-PaletteWriteStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        write_seen = $false
        palette_format = 0
        last_value = 0
        address_map = 0
        write_count = 0
        last_address = 0
        raw_word = 0
        palette_index = 0
        resolved_rgb = 0
        read_seen = $false
        last_read_value = 0
        palette_profile = 0
        readback_matches_last_write = $false
        read_count = 0
        last_read_address = 0
        read_raw_word = 0
        read_palette_index = 0
        read_resolved_rgb = 0
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "palette_write_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 20) {
        $empty.present = $true
        return $empty
    }
    return [pscustomobject]@{
        present = $true
        valid = $true
        write_seen = ([int]$bytes[0] -ne 0)
        palette_format = [int]$bytes[1]
        last_value = [int]$bytes[2]
        address_map = [int]$bytes[3]
        write_count = [int][System.BitConverter]::ToUInt32($bytes, 4)
        last_address = [int][System.BitConverter]::ToUInt32($bytes, 8)
        raw_word = [int][System.BitConverter]::ToUInt16($bytes, 12)
        palette_index = [int][System.BitConverter]::ToUInt16($bytes, 14)
        resolved_rgb = [int][System.BitConverter]::ToUInt32($bytes, 16)
        read_seen = ($bytes.Length -ge 21 -and [int]$bytes[20] -ne 0)
        last_read_value = if ($bytes.Length -ge 22) { [int]$bytes[21] } else { 0 }
        palette_profile = if ($bytes.Length -ge 23) { [int]$bytes[22] } else { 0 }
        readback_matches_last_write =
            ($bytes.Length -ge 24 -and [int]$bytes[23] -ne 0)
        read_count = if ($bytes.Length -ge 40) {
            [int][System.BitConverter]::ToUInt32($bytes, 24)
        } else {
            0
        }
        last_read_address = if ($bytes.Length -ge 40) {
            [int][System.BitConverter]::ToUInt32($bytes, 28)
        } else {
            0
        }
        read_raw_word = if ($bytes.Length -ge 40) {
            [int][System.BitConverter]::ToUInt16($bytes, 32)
        } else {
            0
        }
        read_palette_index = if ($bytes.Length -ge 40) {
            [int][System.BitConverter]::ToUInt16($bytes, 34)
        } else {
            0
        }
        read_resolved_rgb = if ($bytes.Length -ge 40) {
            [int][System.BitConverter]::ToUInt32($bytes, 36)
        } else {
            0
        }
    }
}

function Get-BoardProfileStateSummary {
    param([string]$ScreenshotPath)
    $empty = [pscustomobject]@{
        present = $false
        valid = $false
        version = 0
        vertical = $false
        players = 0
        address_map = 0
        sprite_policy = 0
        sprite_active_area = 0
        sprite_buffering = 0
        palette_format = 0
        text_gfx_source = 0
        input_profile = 0
        io_profile = 0
        palette_profile = 0
        priority_profile = 0
        sprite_chip_pair = 0
        sound_comm_chip = 0
        video_profile = 0
        tc0480scp_profile = 0
        aux_profile = 0
        support_flags = 0
        presentation_flags = 0
        palette_profile_supported = $false
        sprite_pair_supported = $false
        sound_comm_supported = $false
        aux_profile_supported = $false
        irq_profile_supported = $false
        runtime_profile_supported = $false
        clock_profile_12m_4m_8m = $false
        raw_capture_profile = $false
        raw_presented_differs = $false
        uses_real_map = $false
        z80_bank_mask = 0
        z80_current_bank_valid = $false
        m68k_clock_hz = 0
        z80_clock_hz = 0
        ym2610_clock_hz = 0
        frame_rate_hz = 0
        visible_width = 0
        visible_height = 0
        line_pixels = 0
        frame_lines = 0
        vblank_start = 0
        z80_fixed_rom_window = 0
        z80_bank_window = 0
        z80_ram_size = 0
        sound_rom_size = 0
        z80_bank_page_count = 0
        text_gfx_base = 0
        tc0100scn_bg_x_offset = 0
        tc0100scn_text_x_offset = 0
        tc0100scn_text_y_origin = 0
        tc0100scn_positive_text_y_origin = 0
        roz_x_offset = 0
        roz_y_offset = 0
        visible_area_profile_valid = $false
        z80_bank_profile_valid = $false
        vertical_raw_presented_capture = $false
    }
    if ([string]::IsNullOrWhiteSpace($ScreenshotPath)) {
        return $empty
    }
    $path = Get-ScreenshotSidecarPath -ScreenshotPath $ScreenshotPath -ViewName "board_profile_state"
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $empty
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 96) {
        $empty.present = $true
        return $empty
    }

    $support = [int]$bytes[20]
    $presentation = [int]$bytes[21]
    $m68kClock = [int64][System.BitConverter]::ToUInt32($bytes, 24)
    $z80Clock = [int64][System.BitConverter]::ToUInt32($bytes, 28)
    $ymClock = [int64][System.BitConverter]::ToUInt32($bytes, 32)
    $frameRate = [int][System.BitConverter]::ToUInt32($bytes, 36)
    $visibleWidth = [int][System.BitConverter]::ToUInt32($bytes, 40)
    $visibleHeight = [int][System.BitConverter]::ToUInt32($bytes, 44)
    $linePixels = [int][System.BitConverter]::ToUInt32($bytes, 48)
    $frameLines = [int][System.BitConverter]::ToUInt32($bytes, 52)
    $vblankStart = [int][System.BitConverter]::ToUInt32($bytes, 56)
    $fixedWindow = [int][System.BitConverter]::ToUInt32($bytes, 60)
    $bankWindow = [int][System.BitConverter]::ToUInt32($bytes, 64)
    $ramSize = [int][System.BitConverter]::ToUInt32($bytes, 68)
    $soundRomSize = [int][System.BitConverter]::ToUInt32($bytes, 72)
    $bankPages = [int][System.BitConverter]::ToUInt32($bytes, 76)
    $textBase = [int][System.BitConverter]::ToUInt32($bytes, 80)
    $vertical = [int]$bytes[1] -ne 0
    $rawCapture = ($presentation -band 0x02) -ne 0
    $rawPresentedDiffers = ($presentation -band 0x04) -ne 0

    return [pscustomobject]@{
        present = $true
        valid = $true
        version = [int]$bytes[0]
        vertical = $vertical
        players = [int]$bytes[2]
        address_map = [int]$bytes[3]
        sprite_policy = [int]$bytes[4]
        sprite_active_area = [int]$bytes[5]
        sprite_buffering = [int]$bytes[6]
        palette_format = [int]$bytes[7]
        text_gfx_source = [int]$bytes[8]
        input_profile = [int]$bytes[9]
        io_profile = [int]$bytes[10]
        palette_profile = [int]$bytes[11]
        priority_profile = [int]$bytes[12]
        sprite_chip_pair = [int]$bytes[13]
        sound_comm_chip = [int]$bytes[14]
        video_profile = [int]$bytes[15]
        tc0480scp_profile = [int]$bytes[16]
        aux_profile = [int]$bytes[17]
        support_flags = $support
        presentation_flags = $presentation
        palette_profile_supported = ($support -band 0x01) -ne 0
        sprite_pair_supported = ($support -band 0x02) -ne 0
        sound_comm_supported = ($support -band 0x04) -ne 0
        aux_profile_supported = ($support -band 0x08) -ne 0
        irq_profile_supported = ($support -band 0x10) -ne 0
        runtime_profile_supported = ($support -band 0x20) -ne 0
        clock_profile_12m_4m_8m = ($support -band 0x80) -ne 0
        raw_capture_profile = $rawCapture
        raw_presented_differs = $rawPresentedDiffers
        uses_real_map = ($presentation -band 0x08) -ne 0
        z80_bank_mask = [int]$bytes[22]
        z80_current_bank_valid = [int]$bytes[23] -ne 0
        m68k_clock_hz = $m68kClock
        z80_clock_hz = $z80Clock
        ym2610_clock_hz = $ymClock
        frame_rate_hz = $frameRate
        visible_width = $visibleWidth
        visible_height = $visibleHeight
        line_pixels = $linePixels
        frame_lines = $frameLines
        vblank_start = $vblankStart
        z80_fixed_rom_window = $fixedWindow
        z80_bank_window = $bankWindow
        z80_ram_size = $ramSize
        sound_rom_size = $soundRomSize
        z80_bank_page_count = $bankPages
        text_gfx_base = $textBase
        tc0100scn_bg_x_offset = [int][System.BitConverter]::ToInt16($bytes, 84)
        tc0100scn_text_x_offset = [int][System.BitConverter]::ToInt16($bytes, 86)
        tc0100scn_text_y_origin = [int][System.BitConverter]::ToInt16($bytes, 88)
        tc0100scn_positive_text_y_origin = [int][System.BitConverter]::ToInt16($bytes, 90)
        roz_x_offset = [int][System.BitConverter]::ToInt16($bytes, 92)
        roz_y_offset = [int][System.BitConverter]::ToInt16($bytes, 94)
        visible_area_profile_valid =
            ($visibleWidth -gt 0 -and $visibleHeight -gt 0 -and
             $linePixels -ge $visibleWidth -and $frameLines -ge $visibleHeight -and
             $vblankStart -ge $visibleHeight)
        z80_bank_profile_valid =
            ([int]$bytes[22] -eq 3 -and $fixedWindow -eq 0x4000 -and
             $bankWindow -eq 0x4000 -and $ramSize -eq 0x2000 -and $bankPages -gt 0)
        vertical_raw_presented_capture = ($vertical -and $rawCapture -and $rawPresentedDiffers)
    }
}

function Get-RuntimeFeatureEvidence {
    param(
        [Parameter(Mandatory = $true)]$PrioritySummary,
        [Parameter(Mandatory = $true)]$DecodedSpriteSummary,
        [string]$ScreenshotPath = $null
    )
    $final = $PrioritySummary.final_sources
    $irq = Get-IrqStateSummary -ScreenshotPath $ScreenshotPath
    $board = Get-BoardProfileStateSummary -ScreenshotPath $ScreenshotPath
    $soundBank = Get-SoundBankStateSummary -ScreenshotPath $ScreenshotPath
    $soundReset = Get-SoundResetStateSummary -ScreenshotPath $ScreenshotPath
    $watchdog = Get-WatchdogStateSummary -ScreenshotPath $ScreenshotPath
    $mainBus = Get-MainBusStateSummary -ScreenshotPath $ScreenshotPath
    $io = Get-IoOutputStateSummary -ScreenshotPath $ScreenshotPath
    $ioAccess = Get-IoAccessStateSummary -ScreenshotPath $ScreenshotPath
    $palette = Get-PaletteWriteStateSummary -ScreenshotPath $ScreenshotPath
    $spriteControl = Get-SpriteControlStateSummary -ScreenshotPath $ScreenshotPath
    $spriteBuffer = Get-SpriteBufferStateSummary -ScreenshotPath $ScreenshotPath
    return [pscustomobject]@{
        tc0100scn_bg_visible =
            ((Get-SourceCount -Counts $final -Name "tc0100scn_bg0") +
             (Get-SourceCount -Counts $final -Name "tc0100scn_bg1")) -gt 0
        tc0100scn_text_visible =
            (Get-SourceCount -Counts $final -Name "tc0100scn_text") -gt 0
        tc0100scn_secondary_visible =
            ((Get-SourceCount -Counts $final -Name "tc0100scn_secondary_bg0") +
             (Get-SourceCount -Counts $final -Name "tc0100scn_secondary_bg1") +
             (Get-SourceCount -Counts $final -Name "tc0100scn_secondary_text")) -gt 0
        tc0480scp_bg_visible =
            ((Get-SourceCount -Counts $final -Name "tc0480scp_bg0") +
             (Get-SourceCount -Counts $final -Name "tc0480scp_bg1") +
             (Get-SourceCount -Counts $final -Name "tc0480scp_bg2") +
             (Get-SourceCount -Counts $final -Name "tc0480scp_bg3")) -gt 0
        tc0480scp_text_visible =
            (Get-SourceCount -Counts $final -Name "tc0480scp_text") -gt 0
        roz_visible = (Get-SourceCount -Counts $final -Name "roz") -gt 0
        sprite_visible = (Get-SourceCount -Counts $final -Name "sprite") -gt 0
        sprite_decoded_objects =
            if ($DecodedSpriteSummary.valid) { [int]$DecodedSpriteSummary.records } else { 0 }
        sprite_decoded =
            ($DecodedSpriteSummary.valid -and [int]$DecodedSpriteSummary.records -gt 0)
        sprite_control_state_sidecar = [bool]$spriteControl.valid
        sprite_control_buffer_valid = [bool]$spriteControl.buffer_valid
        sprite_control_mode = [int]$spriteControl.mode
        sprite_control_active_area_source = [int]$spriteControl.active_area_source
        sprite_control_buffer_policy = [int]$spriteControl.buffer_policy
        sprite_control_active_area = [int]$spriteControl.active_area
        sprite_control_marker_count = [int]$spriteControl.control_marker_count
        sprite_control_disable_marker_count = [int]$spriteControl.disable_marker_count
        sprite_control_flip_marker_count = [int]$spriteControl.flip_marker_count
        sprite_control_active_area_switch_count =
            [int]$spriteControl.active_area_switch_count
        sprite_control_master_scroll_marker_count =
            [int]$spriteControl.master_scroll_marker_count
        sprite_control_extra_scroll_marker_count =
            [int]$spriteControl.extra_scroll_marker_count
        sprite_control_blank_record_count = [int]$spriteControl.blank_record_count
        sprite_buffer_state_sidecar = [bool]$spriteBuffer.valid
        sprite_buffer_valid = [bool]$spriteBuffer.buffer_valid
        sprite_buffer_policy = [int]$spriteBuffer.policy
        sprite_buffer_source_ram_present = [bool]$spriteBuffer.source_ram_present
        sprite_buffer_overlay_word_mask = [int]$spriteBuffer.overlay_word_mask
        sprite_buffer_overlay_word_count = [int]$spriteBuffer.overlay_word_count
        sprite_buffer_words_per_entry = [int]$spriteBuffer.words_per_entry
        sprite_buffer_delayed_source_used = [bool]$spriteBuffer.delayed_source_used
        sprite_buffer_current_copy_to_latched =
            [bool]$spriteBuffer.current_copy_to_latched
        sprite_buffer_current_copy_to_delay =
            [bool]$spriteBuffer.current_copy_to_delay
        sprite_buffer_partial_overlay_profile =
            [bool]$spriteBuffer.partial_overlay_profile
        sprite_buffer_source_ram_bytes = [int]$spriteBuffer.source_ram_bytes
        sprite_buffer_latched_buffer_bytes = [int]$spriteBuffer.latched_buffer_bytes
        sprite_buffer_delay_buffer_bytes = [int]$spriteBuffer.delay_buffer_bytes
        sprite_buffer_overlay_entry_count = [int]$spriteBuffer.overlay_entry_count
        sprite_buffer_overlay_word_total = [int]$spriteBuffer.overlay_word_total
        sprite_buffer_current_to_latched_bytes =
            [int]$spriteBuffer.current_to_latched_bytes
        sprite_buffer_current_to_delay_bytes = [int]$spriteBuffer.current_to_delay_bytes
        sprite_buffer_delayed_source_bytes = [int]$spriteBuffer.delayed_source_bytes
        sprite_priority_reject_pixels = [long]$PrioritySummary.sprite_priority_reject_pixels
        sprite_occupancy_reject_pixels = [long]$PrioritySummary.sprite_occupancy_reject_pixels
        layer_priority_reject_pixels = [long]$PrioritySummary.layer_priority_reject_pixels
        scene_capture_artifact =
            (-not [string]::IsNullOrWhiteSpace($ScreenshotPath) -and
             (Test-Path -LiteralPath $ScreenshotPath -PathType Leaf) -and
             (Test-PpmNonBlank -Path $ScreenshotPath))
        scene_capture_priority_trace = [bool]$PrioritySummary.valid
        scene_capture_decoded_sprite_trace = [bool]$DecodedSpriteSummary.valid
        video_regs_raw_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "video_regs_raw"
        video_regs_secondary_raw_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "video_regs_secondary_raw"
        sprite_bank_regs_raw_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "sprite_bank_regs_raw"
        priority_regs_raw_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "priority_regs_raw"
        roz_control_regs_raw_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "roz_control_regs_raw"
        tc0480scp_control_regs_raw_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "tc0480scp_control_regs_raw"
        irq_state_sidecar = [bool]$irq.valid
        board_profile_sidecar = [bool]$board.valid
        board_profile_supported = [bool]$board.runtime_profile_supported
        board_clock_profile_12m_4m_8m = [bool]$board.clock_profile_12m_4m_8m
        raw_presented_capture_profile = [bool]$board.raw_capture_profile
        vertical_raw_presented_capture_profile =
            [bool]$board.vertical_raw_presented_capture
        visible_area_profile_sidecar = [bool]$board.visible_area_profile_valid
        z80_bank_profile_sidecar = [bool]$board.z80_bank_profile_valid
        board_uses_real_map = [bool]$board.uses_real_map
        board_profile_players = [int]$board.players
        board_profile_visible_width = [int]$board.visible_width
        board_profile_visible_height = [int]$board.visible_height
        board_profile_frame_lines = [int]$board.frame_lines
        board_profile_z80_bank_pages = [int]$board.z80_bank_page_count
        board_profile_sound_rom_size = [int]$board.sound_rom_size
        vblank_irq_level_configured = [int]$irq.configured_vblank_irq_level
        sprite_dma_irq_level_configured = [int]$irq.configured_sprite_dma_irq_level
        vblank_irq_last_assert_level = [int]$irq.last_vblank_irq_level
        m68k_irq_last_ack_level = [int]$irq.last_irq_ack_level
        vblank_irq_raised_count = [int64]$irq.vblank_irq_raised
        vblank_irq_acked_count = [int64]$irq.vblank_irq_acked
        vblank_irq_asserted = [bool]$irq.vblank_irq_asserted
        m68k_autovector_irq_ack_seen = [bool]$irq.irq_ack_seen
        m68k_irq2_placeholder_seen = [bool]$irq.irq2_placeholder_seen
        sprite_dma_irq_last_assert_level = [int]$irq.last_sprite_dma_irq_level
        sprite_dma_irq_last_ack_level = [int]$irq.last_sprite_dma_irq_ack_level
        sprite_dma_irq_raised_count = [int64]$irq.sprite_dma_irq_raised
        sprite_dma_irq_acked_count = [int64]$irq.sprite_dma_irq_acked
        sprite_dma_irq_asserted = [bool]$irq.sprite_dma_irq_asserted
        sprite_dma_irq_ack_seen = [bool]$irq.sprite_dma_irq_ack_seen
        vblank_irq_pending = [bool]$irq.vblank_irq_pending
        sprite_dma_irq_pending = [bool]$irq.sprite_dma_irq_pending
        main_irq_line_level = [int]$irq.main_irq_line_level
        sprite_latched_ram_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "sprite_latched_ram"
        sprite_rendered_ram_sidecar =
            Test-ScreenshotSidecarPresent -ScreenshotPath $ScreenshotPath `
                -ViewName "sprite_rendered_ram"
        board_profile_sprite_policy = [int]$board.sprite_policy
        board_profile_sprite_active_area = [int]$board.sprite_active_area
        board_profile_sprite_buffering = [int]$board.sprite_buffering
        board_profile_palette_format = [int]$board.palette_format
        board_profile_text_gfx_source = [int]$board.text_gfx_source
        board_profile_input_profile = [int]$board.input_profile
        board_profile_io_profile = [int]$board.io_profile
        board_profile_palette_profile = [int]$board.palette_profile
        board_profile_priority_profile = [int]$board.priority_profile
        board_profile_sprite_chip_pair = [int]$board.sprite_chip_pair
        board_profile_sound_comm_chip = [int]$board.sound_comm_chip
        board_profile_video_profile = [int]$board.video_profile
        board_profile_tc0480scp_profile = [int]$board.tc0480scp_profile
        board_profile_aux_profile = [int]$board.aux_profile
        tc0100scn_manifest_offsets_sidecar =
            ([bool]$board.valid -and
             ([int]$board.tc0100scn_bg_x_offset -ne 0 -or
              [int]$board.tc0100scn_text_x_offset -ne 0))
        tc0100scn_manifest_text_y_origin_sidecar =
            ([bool]$board.valid -and
             ([int]$board.tc0100scn_text_y_origin -ne 0 -or
              [int]$board.tc0100scn_positive_text_y_origin -ne 0))
        roz_manifest_offsets_sidecar =
            ([bool]$board.valid -and
             ([int]$board.roz_x_offset -ne 0 -or [int]$board.roz_y_offset -ne 0))
        sound_bank_state_sidecar = [bool]$soundBank.valid
        sound_bank_current = [int]$soundBank.bank
        sound_bank_page = [int]$soundBank.page
        sound_bank_page_count = [int]$soundBank.page_count
        sound_bank_page_valid = [bool]$soundBank.page_valid
        sound_bank_nonzero = ([bool]$soundBank.valid -and [int]$soundBank.bank -ne 0)
        sound_reset_state_sidecar = [bool]$soundReset.valid
        sound_reset_write_seen = [bool]$soundReset.write_seen
        sound_reset_write_count = [int]$soundReset.write_count
        sound_reset_assert_count = [int]$soundReset.assert_count
        sound_reset_release_count = [int]$soundReset.release_count
        sound_reset_line_held = [bool]$soundReset.reset_held
        sound_reset_z80_line = [bool]$soundReset.z80_reset_line
        sound_reset_tc0140syt_profile = [bool]$soundReset.tc0140syt_profile
        sound_reset_sound_rom_present = [bool]$soundReset.sound_rom_present
        sound_reset_main_port = [int]$soundReset.main_port
        sound_reset_sound_port = [int]$soundReset.sound_port
        watchdog_state_sidecar = [bool]$watchdog.valid
        watchdog_write_seen = [bool]$watchdog.write_seen
        watchdog_confirmed_seen = [bool]$watchdog.confirmed_seen
        watchdog_suspect_seen = [bool]$watchdog.suspect_seen
        watchdog_write_count = [int]$watchdog.write_count
        watchdog_confirmed_write_count = [int]$watchdog.confirmed_write_count
        watchdog_suspect_write_count = [int]$watchdog.suspect_write_count
        watchdog_last_address = [int]$watchdog.last_address
        watchdog_last_window = [int]$watchdog.last_window
        watchdog_confirmed_window_present = [bool]$watchdog.confirmed_window_present
        watchdog_suspect_window_present = [bool]$watchdog.suspect_window_present
        watchdog_timeout_model_supported = [bool]$watchdog.timeout_model_supported
        watchdog_reset_model_supported = [bool]$watchdog.reset_model_supported
        watchdog_priority_suspect_address = [int]$watchdog.priority_suspect_address
        main_bus_state_sidecar = [bool]$mainBus.valid
        main_bus_observer_installed = [bool]$mainBus.observer_installed
        main_bus_byte_observer_supported = [bool]$mainBus.byte_observer_supported
        main_bus_wait_state_model_supported = [bool]$mainBus.wait_state_model_supported
        main_bus_read_count = [int]$mainBus.read_count
        main_bus_write_count = [int]$mainBus.write_count
        main_bus_open_bus_read_count = [int]$mainBus.open_bus_read_count
        main_bus_unmapped_write_count = [int]$mainBus.unmapped_write_count
        main_bus_odd_access_count = [int]$mainBus.odd_access_count
        main_bus_inferred_word_pair_count = [int]$mainBus.inferred_word_pair_count
        main_bus_last_address = [int]$mainBus.last_address
        main_bus_last_value = [int]$mainBus.last_value
        main_bus_last_mapped = [bool]$mainBus.last_mapped
        main_bus_last_open_bus = [bool]$mainBus.last_open_bus
        main_bus_last_pair_inferred = [bool]$mainBus.last_pair_inferred
        io_output_state_sidecar = [bool]$io.valid
        io_output_write_seen = [bool]$io.write_seen
        io_output_write_count = [int]$io.write_count
        io_coin_counter_edges = [int]$io.coin_counter_edges
        io_coin_counter_slots = [int]$io.coin_counter_slots
        io_coin_lockout_mask = [int]$io.coin_lockout_mask
        io_coin_lockout_slots = [int]$io.coin_lockout_slots
        io_cabinet_test_mask = [int]$io.cabinet_test_mask
        io_four_player_service_mask = [int]$io.four_player_service_mask
        io_raw_input_system = [int]$io.raw_input_system
        io_raw_input_coin_extension = [int]$io.raw_input_coin_extension
        io_split_panel_input = [int]$io.split_panel_input
        io_profile_id = [int]$io.io_profile
        io_aux_profile_id = [int]$io.aux_profile
        io_access_state_sidecar = [bool]$ioAccess.valid
        io_access_seen = [bool]$ioAccess.access_seen
        io_access_input_read_count = [int]$ioAccess.input_read_count
        io_access_output_write_count = [int]$ioAccess.output_write_count
        io_access_dip_read_count = [int]$ioAccess.dip_read_count
        io_access_service_read_count = [int]$ioAccess.service_read_count
        io_access_read_even_count = [int]$ioAccess.read_even_count
        io_access_read_odd_count = [int]$ioAccess.read_odd_count
        io_access_write_even_count = [int]$ioAccess.write_even_count
        io_access_write_odd_count = [int]$ioAccess.write_odd_count
        io_access_inferred_read_pair_count = [int]$ioAccess.inferred_read_pair_count
        io_access_inferred_write_pair_count = [int]$ioAccess.inferred_write_pair_count
        io_access_last_address = [int]$ioAccess.last_address
        io_access_last_value = [int]$ioAccess.last_value
        io_access_last_pair_inferred = [bool]$ioAccess.last_pair_inferred
        io_access_cabinet_test_mask = [int]$ioAccess.cabinet_test_mask
        io_access_four_player_service_mask = [int]$ioAccess.four_player_service_mask
        palette_write_state_sidecar = [bool]$palette.valid
        palette_write_seen = [bool]$palette.write_seen
        palette_write_count = [int]$palette.write_count
        palette_format_id = [int]$palette.palette_format
        palette_resolved_rgb = [int]$palette.resolved_rgb
        palette_read_seen = [bool]$palette.read_seen
        palette_read_count = [int]$palette.read_count
        palette_read_resolved_rgb = [int]$palette.read_resolved_rgb
        palette_read_raw_word = [int]$palette.read_raw_word
        palette_readback_matches_last_write = [bool]$palette.readback_matches_last_write
    }
}

function Get-AudioRuntimeEvidence {
    param([Parameter(Mandatory = $true)]$AudioSummary)
    return [pscustomobject]@{
        audio_present = [bool]$AudioSummary.present
        audio_valid = [bool]$AudioSummary.valid
        audio_nonzero =
            ([bool]$AudioSummary.valid -and [int]$AudioSummary.nonzero_frames -gt 0)
        audio_peak_abs = [int]$AudioSummary.peak_abs
        audio_active_windows = [int]$AudioSummary.active_windows
        audio_dropout_suspect = [bool]$AudioSummary.dropout_suspect
        audio_trace_valid = [bool]$AudioSummary.trace_valid
        adpcma_active =
            ([bool]$AudioSummary.trace_valid -and [int]$AudioSummary.adpcma_active_frames -gt 0)
        adpcma_channel_stats_count = [int]$AudioSummary.adpcma_channel_stats_count
        adpcma_active_channel_count = [int]$AudioSummary.adpcma_active_channel_count
        adpcma_channel_cadence_trace =
            ([bool]$AudioSummary.trace_valid -and
             [int]$AudioSummary.adpcma_channel_stats_count -eq 6 -and
             [int]$AudioSummary.adpcma_active_channel_count -gt 0)
        adpcma_channel_dropout_suspect =
            [bool]$AudioSummary.adpcma_channel_dropout_suspect
        adpcma_longest_silence_after_active_ms =
            [int]$AudioSummary.adpcma_longest_silence_after_active_ms_max
        adpcma_key_on_writes = [int]$AudioSummary.adpcma_key_on_writes
        adpcma_key_off_writes = [int]$AudioSummary.adpcma_key_off_writes
        adpcma_rekey_trace = [bool]$AudioSummary.adpcma_rekey_trace_present
        adpcmb_active =
            ([bool]$AudioSummary.trace_valid -and [int]$AudioSummary.adpcmb_active_frames -gt 0)
        adpcmb_control_writes = [int]$AudioSummary.adpcmb_control_writes
        adpcmb_start_writes = [int]$AudioSummary.adpcmb_start_writes
        adpcmb_loop_observed = [bool]$AudioSummary.adpcmb_loop_observed
        adpcm_any_underrun = [bool]$AudioSummary.adpcm_any_underrun
        tc0140syt_present = [bool]$AudioSummary.tc0140syt_present
        tc0140syt_command_nmi_pulse_count =
            [int]$AudioSummary.tc0140syt_command_nmi_pulse_count
        tc0140syt_command_pending_frames =
            [int]$AudioSummary.tc0140syt_command_pending_frames
        tc0140syt_command_write_count =
            [int]$AudioSummary.tc0140syt_command_write_count_max
        tc0140syt_command_read_count =
            [int]$AudioSummary.tc0140syt_command_read_count_max
        tc0140syt_to_adpcma_keyon_latency_trace =
            [bool]$AudioSummary.tc0140syt_to_adpcma_keyon_latency_observed
        tc0140syt_to_adpcma_keyon_latency_min_frames =
            [int]$AudioSummary.tc0140syt_to_adpcma_keyon_latency_min_frames
        tc0140syt_to_adpcma_keyon_latency_max_frames =
            [int]$AudioSummary.tc0140syt_to_adpcma_keyon_latency_max_frames
        tc0140syt_reply_write_count =
            [int]$AudioSummary.tc0140syt_reply_write_count_max
        tc0140syt_reply_read_count =
            [int]$AudioSummary.tc0140syt_reply_read_count_max
        tc0140syt_clear_count =
            [int]$AudioSummary.tc0140syt_clear_count_max
        z80_sound_cpu_present = [bool]$AudioSummary.z80_sound_cpu_present
        z80_nmi_accept_count = [int]$AudioSummary.z80_nmi_accept_count_max
        z80_irq_accept_count = [int]$AudioSummary.z80_irq_accept_count_max
    }
}

function Test-EvidenceFlag {
    param(
        [Parameter(Mandatory = $true)]$Evidence,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $property = $Evidence.PSObject.Properties[$Name]
    return $null -ne $property -and $property.Value -eq $true
}

function Get-EvidenceInt {
    param(
        [Parameter(Mandatory = $true)]$Evidence,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $property = $Evidence.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return 0
    }
    return [int]$property.Value
}

function Test-EvidenceIntEquals {
    param(
        [Parameter(Mandatory = $true)]$Evidence,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$Value
    )
    $property = $Evidence.PSObject.Properties[$Name]
    return $null -ne $property -and $null -ne $property.Value -and
        [int]$property.Value -eq $Value
}

function Test-FeatureObserved {
    param(
        [Parameter(Mandatory = $true)][string]$Feature,
        [Parameter(Mandatory = $true)][object[]]$Evidence
    )
    foreach ($row in $Evidence) {
        switch ($Feature) {
            "tc0100scn_bg_text" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_bg_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "tc0100scn_text_visible")) {
                    return $true
                }
            }
            "tc0100scn_tile_decode_layout" {
                if (((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_bg_visible") -or
                     (Test-EvidenceFlag -Evidence $row -Name "tc0100scn_text_visible")) -and
                    (Test-EvidenceFlag -Evidence $row -Name "video_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0100scn_layer_disable_priority" {
                if (((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_bg_visible") -or
                     (Test-EvidenceFlag -Evidence $row -Name "tc0100scn_text_visible")) -and
                    (Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0100scn_scroll_origin" {
                if (((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_bg_visible") -or
                     (Test-EvidenceFlag -Evidence $row -Name "tc0100scn_text_visible")) -and
                    (Test-EvidenceFlag -Evidence $row -Name "video_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0100scn_bg_y_origin" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_bg_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "video_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0100scn_rowscroll_colscroll" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_bg_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "video_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0100scn_text_priority_model" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_text_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0480scp" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0480scp_bg_visible") -or
                    (Test-EvidenceFlag -Evidence $row -Name "tc0480scp_text_visible")) {
                    return $true
                }
            }
            "tc0480scp_tile_decode_layout" {
                if (((Test-EvidenceFlag -Evidence $row -Name "tc0480scp_bg_visible") -or
                     (Test-EvidenceFlag -Evidence $row -Name "tc0480scp_text_visible")) -and
                    (Test-EvidenceFlag -Evidence $row -Name "tc0480scp_control_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0480scp_control_snapshot_timing" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0480scp_control_regs_raw_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_tc0480scp_profile") -gt 0) {
                    return $true
                }
            }
            "tc0480scp_layer_disable_priority" {
                if (((Test-EvidenceFlag -Evidence $row -Name "tc0480scp_bg_visible") -or
                     (Test-EvidenceFlag -Evidence $row -Name "tc0480scp_text_visible")) -and
                    (Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0480scp_bg_text_offsets" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0480scp_control_regs_raw_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_tc0480scp_profile") -gt 0) {
                    return $true
                }
            }
            "tc0480scp_priority_model" {
                if ((Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_tc0480scp_profile") -gt 0) {
                    return $true
                }
            }
            "tc0480scp_row_zoom_double_width" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0480scp_control_regs_raw_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_tc0480scp_profile") -gt 0) {
                    return $true
                }
            }
            "tc0480scp_rowscroll_colscroll" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0480scp_control_regs_raw_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_tc0480scp_profile") -gt 0) {
                    return $true
                }
            }
            "dual_tc0100scn" {
                if (Test-EvidenceFlag -Evidence $row -Name "tc0100scn_secondary_visible") {
                    return $true
                }
            }
            "dual_tc0100scn_priority_merge" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0100scn_secondary_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar")) {
                    return $true
                }
            }
            "roz" {
                if (Test-EvidenceFlag -Evidence $row -Name "roz_visible") {
                    return $true
                }
            }
            "roz_fixed_point_offsets" {
                if ((Test-EvidenceFlag -Evidence $row -Name "roz_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "roz_control_regs_raw_sidecar")) {
                    return $true
                }
            }
            "roz_priority_palette_bank" {
                if ((Test-EvidenceFlag -Evidence $row -Name "roz_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar")) {
                    return $true
                }
            }
            "roz_wrap_clip_flip_semantics" {
                if ((Test-EvidenceFlag -Evidence $row -Name "roz_visible") -and
                    (Test-EvidenceFlag -Evidence $row -Name "roz_control_regs_raw_sidecar")) {
                    return $true
                }
            }
            "tc0280grd_dual_chip_profile" {
                if ((Test-EvidenceFlag -Evidence $row -Name "roz_control_regs_raw_sidecar") -and
                    (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_video_profile" `
                        -Value 2)) {
                    return $true
                }
            }
            "tc0280grd_multi_chip_register_pair" {
                if ((Test-EvidenceFlag -Evidence $row -Name "roz_control_regs_raw_sidecar") -and
                    (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_video_profile" `
                        -Value 2)) {
                    return $true
                }
            }
            "tc0430grw_chip_profile" {
                if ((Test-EvidenceFlag -Evidence $row -Name "roz_control_regs_raw_sidecar") -and
                    (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_video_profile" `
                        -Value 3)) {
                    return $true
                }
            }
            "tc0190fmc_banked_sprites" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_visible") -or
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_decoded")) {
                    return $true
                }
            }
            "sprite_extension_ram" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_visible") -or
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_decoded")) {
                    return $true
                }
            }
            "scene_capture_matrix_per_board" {
                if ((Test-EvidenceFlag -Evidence $row -Name "scene_capture_artifact") -and
                    (Test-EvidenceFlag -Evidence $row -Name "scene_capture_priority_trace") -and
                    (Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "visible_area_profile_sidecar")) {
                    return $true
                }
            }
            "tc0200obj_policy_banked" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_policy" -Value 2) {
                    return $true
                }
            }
            "tc0200obj_policy_extension_1" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_policy" -Value 3) {
                    return $true
                }
            }
            "tc0200obj_policy_extension_2" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_policy" -Value 4) {
                    return $true
                }
            }
            "tc0200obj_policy_extension_3" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_policy" -Value 5) {
                    return $true
                }
            }
            "tc0200obj_buffering_immediate" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_buffering" -Value 0) {
                    return $true
                }
            }
            "tc0200obj_buffering_full_delayed" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_buffering" -Value 1) {
                    return $true
                }
            }
            "tc0200obj_buffering_partial_delayed" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_buffering" -Value 2) {
                    return $true
                }
            }
            "tc0200obj_buffering_partial_delayed_thundfox" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_buffering" -Value 3) {
                    return $true
                }
            }
            "tc0200obj_buffering_partial_delayed_qzchikyu" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_sprite_buffering" -Value 4) {
                    return $true
                }
            }
            "tc0200obj_active_area_marker" {
                if ((Get-EvidenceInt -Evidence $row -Name "board_profile_sprite_active_area") `
                        -gt 1) {
                    return $true
                }
            }
            "tc0200obj_control_marker_per_map" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_control_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    ((Get-EvidenceInt -Evidence $row -Name "sprite_control_marker_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "sprite_control_master_scroll_marker_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "sprite_control_extra_scroll_marker_count") -gt 0)) {
                    return $true
                }
            }
            "tc0200obj_partial_buffer_byte_lane_profile" {
                $boardPolicy = Get-EvidenceInt -Evidence $row -Name "board_profile_sprite_buffering"
                $sidecarPolicy = Get-EvidenceInt -Evidence $row -Name "sprite_buffer_policy"
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_buffer_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_buffer_valid") -and
                    $sidecarPolicy -eq $boardPolicy -and
                    (Get-EvidenceInt -Evidence $row -Name "sprite_buffer_words_per_entry") -eq 8 -and
                    (Get-EvidenceInt -Evidence $row -Name "sprite_buffer_latched_buffer_bytes") -gt 0) {
                    if ($boardPolicy -le 1) {
                        return $true
                    }
                    if ((Test-EvidenceFlag -Evidence $row -Name "sprite_buffer_partial_overlay_profile") -and
                        (Test-EvidenceFlag -Evidence $row -Name "sprite_buffer_delayed_source_used") -and
                        (Test-EvidenceFlag -Evidence $row -Name "sprite_buffer_current_copy_to_delay") -and
                        (Get-EvidenceInt -Evidence $row -Name "sprite_buffer_overlay_word_mask") -gt 0 -and
                        (Get-EvidenceInt -Evidence $row -Name "sprite_buffer_overlay_word_count") -gt 0) {
                        return $true
                    }
                }
            }
            "tc0200obj_decoded_object_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "sprite_decoded") {
                    return $true
                }
            }
            "tc0200obj_custom_pair_old_new_versions" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "board_profile_supported")) {
                    return $true
                }
            }
            "tc0200obj_sprite_gfx_decode_layout" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_decoded") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_rendered_ram_sidecar")) {
                    return $true
                }
            }
            "tc0200obj_sprite_rom_region_order" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_decoded") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_rendered_ram_sidecar")) {
                    return $true
                }
            }
            "tc0200obj_palette_bank_origin" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_decoded") -and
                    (Test-EvidenceFlag -Evidence $row -Name "palette_write_state_sidecar")) {
                    return $true
                }
            }
            "tc0200obj_master_extra_scroll" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_decoded") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_latched_ram_sidecar")) {
                    return $true
                }
            }
            "tc0200obj_offscreen_wrap_clip" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_decoded") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_rendered_ram_sidecar")) {
                    return $true
                }
            }
            "tc0200obj_sprite_disable_flip_markers" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_decoded") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_rendered_ram_sidecar")) {
                    return $true
                }
            }
            "tc0100scn_register_snapshot_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "video_regs_raw_sidecar") {
                    return $true
                }
            }
            "tc0100scn_text_source_sidecars" {
                if (Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") {
                    return $true
                }
            }
            "tc0100scn_manifest_x_offsets" {
                if (Test-EvidenceFlag -Evidence $row `
                        -Name "tc0100scn_manifest_offsets_sidecar") {
                    return $true
                }
            }
            "tc0100scn_text_y_origin" {
                if (Test-EvidenceFlag -Evidence $row `
                        -Name "tc0100scn_manifest_text_y_origin_sidecar") {
                    return $true
                }
            }
            "roz_manifest_offsets" {
                if (Test-EvidenceFlag -Evidence $row -Name "roz_manifest_offsets_sidecar") {
                    return $true
                }
            }
            "tc0480scp_profile_metalb" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_tc0480scp_profile" -Value 1) {
                    return $true
                }
            }
            "tc0480scp_profile_footchmp" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_tc0480scp_profile" -Value 2) {
                    return $true
                }
            }
            "tc0480scp_profile_deadconx" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_tc0480scp_profile" -Value 3) {
                    return $true
                }
            }
            "tc0480scp_register_snapshot_sidecar" {
                if (Test-EvidenceFlag -Evidence $row `
                        -Name "tc0480scp_control_regs_raw_sidecar") {
                    return $true
                }
            }
            "tc0480scp_text_source_sidecars" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "tc0480scp_text_visible")) {
                    return $true
                }
            }
            "tc0100scn_program_region_text_1bpp" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_text_gfx_source" -Value 1) {
                    return $true
                }
            }
            "text_gfx_source_tc0100scn_ram_2bpp" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_text_gfx_source" -Value 0) {
                    return $true
                }
            }
            "text_gfx_source_program_1bpp" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_text_gfx_source" -Value 1) {
                    return $true
                }
            }
            "palette_xbgr_555" {
                if ((Test-EvidenceIntEquals -Evidence $row -Name "palette_format_id" `
                        -Value 0) -or
                    (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_palette_format" -Value 0)) {
                    return $true
                }
            }
            "palette_rgbx_444" {
                if ((Test-EvidenceIntEquals -Evidence $row -Name "palette_format_id" `
                        -Value 1) -or
                    (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_palette_format" -Value 1)) {
                    return $true
                }
            }
            "palette_xrgb_555" {
                if ((Test-EvidenceIntEquals -Evidence $row -Name "palette_format_id" `
                        -Value 2) -or
                    (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_palette_format" -Value 2)) {
                    return $true
                }
            }
            "palette_rrrr_gggg_bbbb_rgbx" {
                if ((Test-EvidenceIntEquals -Evidence $row -Name "palette_format_id" `
                        -Value 3) -or
                    (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_palette_format" -Value 3)) {
                    return $true
                }
            }
            "ym2610_scene_audio" {
                if ((Test-EvidenceFlag -Evidence $row -Name "audio_nonzero") -and
                    (Test-EvidenceFlag -Evidence $row -Name "audio_trace_valid")) {
                    return $true
                }
            }
            "ym2610_adpcma_samples" {
                if (Test-EvidenceFlag -Evidence $row -Name "adpcma_active") {
                    return $true
                }
            }
            "ym2610_adpcma_channel_cadence" {
                if ((Test-EvidenceFlag -Evidence $row -Name "adpcma_channel_cadence_trace") -and
                    -not (Test-EvidenceFlag -Evidence $row -Name "adpcm_any_underrun")) {
                    return $true
                }
            }
            "ym2610_adpcma_rekey_trace" {
                if (Test-EvidenceFlag -Evidence $row -Name "adpcma_rekey_trace") {
                    return $true
                }
            }
            "ym2610_adpcmb_samples" {
                if (Test-EvidenceFlag -Evidence $row -Name "adpcmb_active") {
                    return $true
                }
            }
            "ym2610_adpcmb_control_writes" {
                if ((Get-EvidenceInt -Evidence $row -Name "adpcmb_control_writes") -gt 0) {
                    return $true
                }
            }
            "ym2610_adpcmb_loop_end_cadence" {
                if ((Get-EvidenceInt -Evidence $row -Name "adpcmb_start_writes") -gt 0 -or
                    (Test-EvidenceFlag -Evidence $row -Name "adpcmb_loop_observed")) {
                    return $true
                }
            }
            "tc0140syt_sound_comm" {
                if ((Test-EvidenceFlag -Evidence $row -Name "tc0140syt_present") -and
                    ((Get-EvidenceInt -Evidence $row -Name "tc0140syt_command_nmi_pulse_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "tc0140syt_command_pending_frames") -gt 0)) {
                    return $true
                }
            }
            "tc0140syt_tc0530syc_sound_comm_profile" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_sound_comm_chip") -ge 0) {
                    return $true
                }
            }
            "tc0140syt_nibble_phase_nmi_ack" {
                if ((Get-EvidenceInt -Evidence $row -Name "tc0140syt_command_write_count") -gt 0 -and
                    (Get-EvidenceInt -Evidence $row -Name "tc0140syt_command_nmi_pulse_count") -gt 0) {
                    return $true
                }
            }
            "tc0140syt_command_consumption_trace" {
                if ((Get-EvidenceInt -Evidence $row -Name "tc0140syt_command_read_count") -gt 0) {
                    return $true
                }
            }
            "tc0140syt_reply_clear_trace" {
                if ((Get-EvidenceInt -Evidence $row -Name "tc0140syt_reply_write_count") -gt 0 -or
                    (Get-EvidenceInt -Evidence $row -Name "tc0140syt_reply_read_count") -gt 0 -or
                    (Get-EvidenceInt -Evidence $row -Name "tc0140syt_clear_count") -gt 0) {
                    return $true
                }
            }
            "sound_cpu_reset_control_line" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sound_reset_state_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "sound_reset_write_count") -gt 0 -and
                    ((Get-EvidenceInt -Evidence $row -Name "sound_reset_assert_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "sound_reset_release_count") -gt 0 -or
                     (Test-EvidenceFlag -Evidence $row -Name "sound_reset_line_held"))) {
                    return $true
                }
            }
            "tc0140syt_reset_callback_trace" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sound_reset_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sound_reset_tc0140syt_profile") -and
                    (Get-EvidenceInt -Evidence $row -Name "sound_reset_write_count") -gt 0 -and
                    ((Test-EvidenceFlag -Evidence $row -Name "sound_reset_z80_line") -or
                     (Get-EvidenceInt -Evidence $row -Name "sound_reset_assert_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "sound_reset_release_count") -gt 0)) {
                    return $true
                }
            }
            "tc0140syt_to_ym2610_command_latency_trace" {
                if ((Test-EvidenceFlag -Evidence $row `
                        -Name "tc0140syt_to_adpcma_keyon_latency_trace") -and
                    (Get-EvidenceInt -Evidence $row -Name "z80_nmi_accept_count") -gt 0) {
                    return $true
                }
            }
            "m68k_z80_ym2610_interleave" {
                if ((Test-EvidenceFlag -Evidence $row -Name "audio_trace_valid") -and
                    (Get-EvidenceInt -Evidence $row -Name "tc0140syt_command_write_count") -gt 0 -and
                    (Get-EvidenceInt -Evidence $row -Name "tc0140syt_command_read_count") -gt 0 -and
                    (Get-EvidenceInt -Evidence $row -Name "z80_nmi_accept_count") -gt 0 -and
                    (Test-EvidenceFlag -Evidence $row `
                        -Name "tc0140syt_to_adpcma_keyon_latency_trace")) {
                    return $true
                }
            }
            "z80_nmi_acceptance_trace" {
                if ((Test-EvidenceFlag -Evidence $row -Name "z80_sound_cpu_present") -and
                    (Get-EvidenceInt -Evidence $row -Name "z80_nmi_accept_count") -gt 0) {
                    return $true
                }
            }
            "z80_sound_bank_latch_semantics" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sound_bank_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sound_bank_page_valid")) {
                    return $true
                }
            }
            "z80_sound_bank_switch_trace" {
                if (Test-EvidenceFlag -Evidence $row -Name "sound_bank_nonzero") {
                    return $true
                }
            }
            "z80_sound_bank_mask_page_size_by_board" {
                if (Test-EvidenceFlag -Evidence $row -Name "z80_bank_profile_sidecar") {
                    return $true
                }
            }
            "z80_sound_program" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_sound_rom_size") -gt 0) {
                    return $true
                }
            }
            "z80_sound_rom_bank_region_layout" {
                if (Test-EvidenceFlag -Evidence $row -Name "z80_bank_profile_sidecar") {
                    return $true
                }
            }
            "f2_physical_chip_profile_manifest" {
                if (Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") {
                    return $true
                }
            }
            "f2_custom_chip_revision_profile" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "board_profile_supported")) {
                    return $true
                }
            }
            "f2_board_clock_profile_12m_4m_8m" {
                if (Test-EvidenceFlag -Evidence $row -Name "board_clock_profile_12m_4m_8m") {
                    return $true
                }
            }
            "f2_address_map_vs_chip_profile_split" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "board_uses_real_map")) {
                    return $true
                }
            }
            "video_frame_timing_raw_presented_area" {
                if ((Test-EvidenceFlag -Evidence $row -Name "visible_area_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "raw_presented_capture_profile")) {
                    return $true
                }
            }
            "exact_display_timing_visible_area" {
                if (Test-EvidenceFlag -Evidence $row -Name "visible_area_profile_sidecar") {
                    return $true
                }
            }
            "vertical_raw_presented_capture" {
                if (Test-EvidenceFlag -Evidence $row -Name "vertical_raw_presented_capture_profile") {
                    return $true
                }
            }
            "vertical_presentation" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_visible_height") -gt 0) {
                    return $true
                }
            }
            "m68k_autovector_irq_level_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "irq_state_sidecar") {
                    return $true
                }
            }
            "m68k_irq2_placeholder_guard" {
                if ((Test-EvidenceFlag -Evidence $row -Name "irq_state_sidecar") -and
                    -not (Test-EvidenceFlag -Evidence $row -Name "m68k_irq2_placeholder_seen")) {
                    return $true
                }
            }
            "vblank_irq_timing" {
                if (Test-EvidenceFlag -Evidence $row -Name "vblank_irq_asserted") {
                    return $true
                }
            }
            "vblank_irq_level_by_board" {
                $configured = Get-EvidenceInt -Evidence $row -Name "vblank_irq_level_configured"
                $asserted = Get-EvidenceInt -Evidence $row -Name "vblank_irq_last_assert_level"
                if ((Test-EvidenceFlag -Evidence $row -Name "vblank_irq_asserted") -and
                    $configured -gt 0 -and $asserted -eq $configured) {
                    return $true
                }
            }
            "m68k_irq_ack_vector_timing" {
                $configured = Get-EvidenceInt -Evidence $row -Name "vblank_irq_level_configured"
                $acked = Get-EvidenceInt -Evidence $row -Name "m68k_irq_last_ack_level"
                if ((Test-EvidenceFlag -Evidence $row -Name "m68k_autovector_irq_ack_seen") -and
                    $configured -gt 0 -and $acked -eq $configured) {
                    return $true
                }
            }
            "m68k_irq5_irq6_vbl_dma_mapping" {
                if ((Get-EvidenceInt -Evidence $row -Name "vblank_irq_level_configured") -eq 5 -and
                    (Get-EvidenceInt -Evidence $row -Name "sprite_dma_irq_level_configured") -eq 6) {
                    return $true
                }
            }
            "m68k_bus_wait_open_bus_width" {
                if ((Test-EvidenceFlag -Evidence $row -Name "main_bus_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "main_bus_byte_observer_supported") -and
                    ((Get-EvidenceInt -Evidence $row -Name "main_bus_open_bus_read_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "main_bus_unmapped_write_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "main_bus_inferred_word_pair_count") -gt 0)) {
                    return $true
                }
            }
            "m68k_byte_word_access_width_trace" {
                if ((Test-EvidenceFlag -Evidence $row -Name "main_bus_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "main_bus_byte_observer_supported") -and
                    (Get-EvidenceInt -Evidence $row -Name "main_bus_inferred_word_pair_count") -gt 0) {
                    return $true
                }
            }
            "address_map_unmapped_open_bus_sidecar" {
                if ((Test-EvidenceFlag -Evidence $row -Name "main_bus_state_sidecar") -and
                    ((Get-EvidenceInt -Evidence $row -Name "main_bus_open_bus_read_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "main_bus_unmapped_write_count") -gt 0)) {
                    return $true
                }
            }
            "tc0200obj_dma_irq_buffer_timing" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_latched_ram_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_dma_irq_asserted")) {
                    return $true
                }
            }
            "sprite_dma_irq_assert_ack_timing" {
                $configured = Get-EvidenceInt -Evidence $row -Name "sprite_dma_irq_level_configured"
                $asserted = Get-EvidenceInt -Evidence $row -Name "sprite_dma_irq_last_assert_level"
                $acked = Get-EvidenceInt -Evidence $row -Name "sprite_dma_irq_last_ack_level"
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_dma_irq_asserted") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_dma_irq_ack_seen") -and
                    $configured -gt 0 -and $asserted -eq $configured -and
                    $acked -eq $configured) {
                    return $true
                }
            }
            "board_raw_register_window_sidecars" {
                if ((Test-EvidenceFlag -Evidence $row -Name "video_regs_raw_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar")) {
                    return $true
                }
            }
            "board_io_output_sidecars" {
                if (Test-EvidenceFlag -Evidence $row -Name "io_output_state_sidecar") {
                    return $true
                }
            }
            "board_input_dip_watchdog" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "io_output_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "watchdog_state_sidecar")) {
                    return $true
                }
            }
            "coin_service_watchdog_reset_semantics" {
                if ((Get-EvidenceInt -Evidence $row -Name "io_coin_counter_edges") -gt 0 -or
                    (Get-EvidenceInt -Evidence $row -Name "io_coin_lockout_mask") -ne 0 -or
                    (Get-EvidenceInt -Evidence $row -Name "io_four_player_service_mask") -ne 0 -or
                    (Test-EvidenceFlag -Evidence $row -Name "watchdog_write_seen")) {
                    return $true
                }
            }
            "watchdog_timer_reset_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "watchdog_state_sidecar") {
                    return $true
                }
            }
            "watchdog_address_map_write_windows" {
                if ((Test-EvidenceFlag -Evidence $row -Name "watchdog_state_sidecar") -and
                    ((Test-EvidenceFlag -Evidence $row `
                        -Name "watchdog_confirmed_window_present") -or
                     (Test-EvidenceFlag -Evidence $row `
                        -Name "watchdog_suspect_window_present"))) {
                    return $true
                }
            }
            "io_device_byte_lane_width_semantics" {
                if ((Test-EvidenceFlag -Evidence $row -Name "io_access_state_sidecar") -and
                    ((Get-EvidenceInt -Evidence $row -Name "io_access_inferred_read_pair_count") -gt 0 -or
                     (Get-EvidenceInt -Evidence $row -Name "io_access_inferred_write_pair_count") -gt 0 -or
                     ((Get-EvidenceInt -Evidence $row -Name "io_access_read_even_count") -gt 0 -and
                      (Get-EvidenceInt -Evidence $row -Name "io_access_read_odd_count") -gt 0) -or
                     ((Get-EvidenceInt -Evidence $row -Name "io_access_write_even_count") -gt 0 -and
                      (Get-EvidenceInt -Evidence $row -Name "io_access_write_odd_count") -gt 0))) {
                    return $true
                }
            }
            "service_dip_mux_readback_trace" {
                if ((Test-EvidenceFlag -Evidence $row -Name "io_access_state_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "io_access_dip_read_count") -gt 0 -and
                    (Get-EvidenceInt -Evidence $row -Name "io_access_service_read_count") -gt 0) {
                    return $true
                }
            }
            "service_test_input_path" {
                if ((Get-EvidenceInt -Evidence $row -Name "io_cabinet_test_mask") -ne 0 -or
                    (Get-EvidenceInt -Evidence $row -Name "io_four_player_service_mask") -ne 0) {
                    return $true
                }
            }
            "cabinet_test_switch_input" {
                if ((Get-EvidenceInt -Evidence $row -Name "io_cabinet_test_mask") -ne 0) {
                    return $true
                }
            }
            "input_profile_standard" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_input_profile" -Value 0) {
                    return $true
                }
            }
            "input_profile_split_tmp82c265" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_input_profile" -Value 1) {
                    return $true
                }
            }
            "input_profile_te7750_quad" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_input_profile" -Value 2) {
                    return $true
                }
            }
            "io_custom_identity_profile" {
                if (Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") {
                    return $true
                }
            }
            "four_player_io" {
                if ((Get-EvidenceInt -Evidence $row -Name "board_profile_players") -ge 4) {
                    return $true
                }
            }
            "four_player_coin_counter_lockout_outputs" {
                if ((Get-EvidenceInt -Evidence $row -Name "board_profile_players") -ge 4 -and
                    (Get-EvidenceInt -Evidence $row -Name "io_coin_counter_slots") -ge 4 -and
                    (Get-EvidenceInt -Evidence $row -Name "io_coin_lockout_slots") -ge 4) {
                    return $true
                }
            }
            "four_player_coin_counter_slots" {
                if ((Get-EvidenceInt -Evidence $row -Name "board_profile_players") -ge 4 -and
                    (Get-EvidenceInt -Evidence $row -Name "io_coin_counter_slots") -ge 4) {
                    return $true
                }
            }
            "four_player_service_test_mux" {
                $players = Get-EvidenceInt -Evidence $row -Name "board_profile_players"
                $inputProfile = Get-EvidenceInt -Evidence $row -Name "board_profile_input_profile"
                $serviceMask = Get-EvidenceInt -Evidence $row -Name "io_four_player_service_mask"
                if ($players -ge 4 -and
                    (($inputProfile -eq 1 -and $serviceMask -eq 12) -or
                     ($inputProfile -eq 2 -and $serviceMask -eq 24))) {
                    return $true
                }
            }
            "tmp82c265_panel_coin_outputs" {
                if (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 3) {
                    return $true
                }
            }
            "te7750_quad_player_mux" {
                if (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 2) {
                    return $true
                }
            }
            "tc0220ioc_tc0510nio_input_mux" {
                if ((Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 0) -or
                    (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 1)) {
                    return $true
                }
            }
            "shifted_quiz_io" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    ((Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 0) -or
                     (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 1))) {
                    return $true
                }
            }
            "tc0220ioc_shifted_quiz_mux" {
                if ((Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 0) -or
                    (Test-EvidenceIntEquals -Evidence $row -Name "board_profile_io_profile" `
                        -Value 1)) {
                    return $true
                }
            }
            "tc0100scn_raw_scroll_register_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "video_regs_raw_sidecar") {
                    return $true
                }
            }
            "dual_tc0100scn_secondary_register_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "video_regs_secondary_raw_sidecar") {
                    return $true
                }
            }
            "tc0480scp_raw_control_register_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "tc0480scp_control_regs_raw_sidecar") {
                    return $true
                }
            }
            "tc0360pri_raw_register_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "priority_regs_raw_sidecar") {
                    return $true
                }
            }
            "tc0110pcr_tc0260dar_tc0070rgb_palette_profile" {
                if ((Test-EvidenceFlag -Evidence $row -Name "board_profile_sidecar") -and
                    (Get-EvidenceInt -Evidence $row -Name "board_profile_palette_profile") -ge 0) {
                    return $true
                }
            }
            "tc0110pcr_palette_readback" {
                if ((Test-EvidenceFlag -Evidence $row -Name "palette_write_state_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "palette_read_seen") -and
                    (Get-EvidenceInt -Evidence $row -Name "palette_read_count") -gt 0) {
                    return $true
                }
            }
            "tc0260dar_known_roster_profile" {
                if (Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_palette_profile" -Value 1) {
                    return $true
                }
            }
            "tc0260dar_runtime_support" {
                if ((Test-EvidenceIntEquals -Evidence $row `
                        -Name "board_profile_palette_profile" -Value 1) -and
                    (Test-EvidenceFlag -Evidence $row -Name "board_profile_supported")) {
                    return $true
                }
            }
            "roz_raw_control_register_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "roz_control_regs_raw_sidecar") {
                    return $true
                }
            }
            "tc0190fmc_raw_bank_register_sidecar" {
                if (Test-EvidenceFlag -Evidence $row -Name "sprite_bank_regs_raw_sidecar") {
                    return $true
                }
            }
            "tc0200obj_latched_vs_current_ram_sidecars" {
                if ((Test-EvidenceFlag -Evidence $row -Name "sprite_latched_ram_sidecar") -and
                    (Test-EvidenceFlag -Evidence $row -Name "sprite_rendered_ram_sidecar")) {
                    return $true
                }
            }
            "tc0360pri_priority_blend" {
                if ((Get-EvidenceInt -Evidence $row -Name "sprite_priority_reject_pixels") -gt 0 -or
                    (Get-EvidenceInt -Evidence $row -Name "layer_priority_reject_pixels") -gt 0) {
                    return $true
                }
            }
        }
    }
    return $false
}

function Get-TaitoF2ObservationRuleNames {
    $rules = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    if ([string]::IsNullOrWhiteSpace($PSCommandPath) -or
        -not (Test-Path -LiteralPath $PSCommandPath -PathType Leaf)) {
        return ,$rules
    }

    $source = Get-Content -LiteralPath $PSCommandPath -Raw
    $match = [regex]::Match(
        $source,
        '(?s)function Test-FeatureObserved\s*\{(?<body>.*?)\r?\nfunction Get-TaitoF2RuntimeFeatureCoverage')
    if (-not $match.Success) {
        return ,$rules
    }

    foreach ($case in [regex]::Matches($match.Groups["body"].Value, '(?m)^\s+"([^"]+)"\s+\{')) {
        [void]$rules.Add($case.Groups[1].Value)
    }
    return ,$rules
}

function Get-TaitoF2PreRuntimeAuditDebt {
    param(
        [Parameter(Mandatory = $true)][object[]]$ManifestProfiles,
        [Parameter(Mandatory = $true)][object[]]$RuntimeFeatureCoverage
    )
    $unmapped = @($RuntimeFeatureCoverage |
        Where-Object { -not $_.has_observation_rule } |
        Sort-Object feature |
        ForEach-Object {
            [pscustomobject]@{
                feature = $_.feature
                evidence = $_.evidence
                sets = @($_.sets)
            }
        })
    $tc0260DarProfileDebt = @($ManifestProfiles |
        Where-Object {
            $_.features -contains "tc0260dar_known_roster_profile" -and
            [string]$_.palette_profile -ne "tc0260dar"
        } |
        Sort-Object set |
        ForEach-Object {
            [pscustomobject]@{
                set = $_.set
                map = $_.map
                palette_profile = $_.palette_profile
                expected_profile = "tc0260dar"
            }
        })

    return [pscustomobject]@{
        observation_rule_count =
            @($RuntimeFeatureCoverage | Where-Object { $_.has_observation_rule }).Count
        unmapped_runtime_feature_gates = @($unmapped)
        tc0260dar_profile_mismatches = @($tc0260DarProfileDebt)
        high_risk_uninstrumented_classes = @(
            "raster_midframe_video_writes",
            "ym2610_scene_loop_waveform_compare",
            "tc0530syc_runtime_support",
            "tc0540obn_tc0520tbc_runtime_support",
            "aux_peripheral_protection_rtc_profile"
        )
    }
}

function Get-TaitoF2RuntimeFeatureCoverage {
    param(
        [Parameter(Mandatory = $true)][object[]]$ManifestProfiles,
        [Parameter(Mandatory = $true)][object[]]$Results
    )
    $visualFeatures = @(
        "tc0100scn_bg_text",
        "tc0100scn_tile_decode_layout",
        "tc0100scn_text_source_sidecars",
        "tc0100scn_layer_disable_priority",
        "tc0480scp",
        "tc0480scp_tile_decode_layout",
        "tc0480scp_text_source_sidecars",
        "tc0480scp_control_snapshot_timing",
        "tc0480scp_layer_disable_priority",
        "dual_tc0100scn",
        "roz",
        "tc0280grd_dual_chip_profile",
        "tc0280grd_multi_chip_register_pair",
        "tc0430grw_chip_profile",
        "roz_wrap_clip_flip_semantics",
        "tc0190fmc_banked_sprites",
            "tc0190fmc_bank_latch_timing",
            "tc0200obj_custom_pair_old_new_versions",
            "tc0200obj_blank_record_bank_guard",
            "sprite_extension_ram",
            "scene_capture_matrix_per_board",
            "tile_rom_region_layout_provenance",
            "secondary_tile_rom_region_layout_provenance",
            "tc0200obj_decoded_object_sidecar",
            "tc0200obj_sprite_gfx_decode_layout",
            "tc0200obj_sprite_rom_region_order",
            "tc0200obj_palette_bank_origin",
            "tc0200obj_extension_code_timing",
            "tc0200obj_master_extra_scroll",
            "tc0200obj_zoom_continuation",
            "tc0200obj_control_marker_per_map",
            "tc0200obj_partial_buffer_byte_lane_profile",
            "tc0200obj_sprite_disable_flip_markers",
            "tc0200obj_offscreen_wrap_clip"
        )
    $videoSidecarFeatures = @(
        "board_raw_register_window_sidecars",
        "tc0100scn_register_snapshot_sidecar",
        "tc0100scn_raw_scroll_register_sidecar",
        "dual_tc0100scn_secondary_register_sidecar",
        "tc0480scp_register_snapshot_sidecar",
        "tc0480scp_raw_control_register_sidecar",
        "tc0360pri_raw_register_sidecar",
        "roz_raw_control_register_sidecar",
        "tc0190fmc_raw_bank_register_sidecar",
        "tc0200obj_latched_vs_current_ram_sidecars",
        "tc0200obj_dma_irq_buffer_timing",
        "tc0200obj_hide_pixel_offsets",
        "tc0360pri_priority_blend",
        "tc0110pcr_tc0260dar_tc0070rgb_palette_profile",
        "m68k_autovector_irq_level_sidecar",
        "m68k_irq_ack_vector_timing",
        "m68k_irq2_placeholder_guard",
        "vblank_irq_timing",
        "vblank_irq_level_by_board",
            "m68k_irq5_irq6_vbl_dma_mapping",
            "m68k_z80_ym2610_interleave",
            "raster_midframe_video_writes",
            "sprite_dma_irq_assert_ack_timing"
        )
    $mainBusFeatures = @(
        "m68k_bus_wait_open_bus_width",
        "m68k_byte_word_access_width_trace",
        "address_map_unmapped_open_bus_sidecar"
    )
    $inputFeatures = @(
        "board_input_dip_watchdog",
        "io_custom_identity_profile",
        "board_io_output_sidecars",
            "coin_service_watchdog_reset_semantics",
            "watchdog_timer_reset_sidecar",
            "watchdog_address_map_write_windows",
            "service_test_input_path",
            "cabinet_test_switch_input",
            "dip_switch_defaults_by_set",
        "input_profile_standard",
        "input_profile_split_tmp82c265",
        "input_profile_te7750_quad",
        "shifted_quiz_io",
        "tc0220ioc_shifted_quiz_mux",
        "four_player_io",
        "four_player_coin_counter_lockout_outputs",
        "four_player_coin_counter_slots",
        "four_player_service_test_mux",
        "tmp82c265_panel_coin_outputs",
        "te7750_quad_player_mux",
            "tc0220ioc_tc0510nio_input_mux"
    )
    $ioAccessFeatures = @(
        "io_device_byte_lane_width_semantics",
        "service_dip_mux_readback_trace"
    )
    $paletteFeatures = @(
        "palette_xbgr_555",
        "palette_rgbx_444",
        "palette_xrgb_555",
        "palette_rrrr_gggg_bbbb_rgbx",
        "tc0110pcr_palette_readback",
        "tc0110pcr_tc0260dar_tc0070rgb_palette_profile",
        "tc0260dar_runtime_support"
    )
    $audioFeatures = @(
        "ym2610_scene_audio",
        "ym2610_fm_lfo_ssgeg_csm",
        "ym2610_timer_irq_sound_cpu",
        "ym2610_adpcma_samples",
            "ym2610_adpcma_channel_cadence",
            "ym2610_adpcma_rekey_trace",
            "ym2610_adpcmb_samples",
            "ym2610_adpcmb_control_writes",
            "ym2610_adpcmb_loop_end_cadence",
            "ym2610_scene_loop_waveform_compare",
            "ym2610_dac_route_filter_profile",
            "ym2610_pan_level_mix",
            "ym2610_audio_savestate_phase",
            "tc0140syt_sound_comm",
            "tc0140syt_nibble_phase_nmi_ack",
            "tc0140syt_tc0530syc_sound_comm_profile",
            "tc0140syt_command_consumption_trace",
            "tc0140syt_reply_clear_trace",
            "z80_nmi_acceptance_trace",
            "tc0140syt_to_ym2610_command_latency_trace",
        "tc0530syc_runtime_support"
    )
    $soundResetFeatures = @(
        "sound_cpu_reset_control_line",
        "tc0140syt_reset_callback_trace"
    )
    $watchdogFeatures = @(
        "watchdog_timer_reset_sidecar",
        "watchdog_address_map_write_windows"
    )
    $boardProfileFeatures = @(
        "f2_physical_chip_profile_manifest",
        "f2_custom_chip_revision_profile",
        "f2_board_clock_profile_12m_4m_8m",
        "f2_address_map_vs_chip_profile_split",
        "video_frame_timing_raw_presented_area",
        "exact_display_timing_visible_area",
        "vertical_raw_presented_capture",
        "vertical_presentation",
        "tc0200obj_policy_banked",
        "tc0200obj_policy_extension_1",
        "tc0200obj_policy_extension_2",
        "tc0200obj_policy_extension_3",
        "tc0200obj_buffering_immediate",
        "tc0200obj_buffering_full_delayed",
        "tc0200obj_buffering_partial_delayed",
        "tc0200obj_buffering_partial_delayed_thundfox",
        "tc0200obj_buffering_partial_delayed_qzchikyu",
        "tc0200obj_active_area_marker",
        "tc0100scn_program_region_text_1bpp",
            "text_gfx_source_tc0100scn_ram_2bpp",
            "text_gfx_source_program_1bpp",
            "tc0100scn_scene_bbox_origin_matrix",
            "tc0480scp_scene_bbox_origin_matrix",
            "roz_flip_scene_offsets",
            "tc0260dar_known_roster_profile",
            "tc0480scp_profile_metalb",
        "tc0480scp_profile_footchmp",
        "tc0480scp_profile_deadconx",
        "tc0540obn_tc0520tbc_runtime_support",
        "tc0030cmd_cchip_runtime_support",
        "rtc_runtime_support",
        "printer_runtime_support"
    )
    $soundBankFeatures = @(
        "z80_sound_bank_latch_semantics",
        "z80_sound_bank_switch_trace",
        "z80_sound_bank_mask_page_size_by_board",
        "z80_sound_program",
        "z80_sound_rom_bank_region_layout"
    )
    $observationRules = Get-TaitoF2ObservationRuleNames
    $runtimeRows = [System.Collections.Generic.List[object]]::new()
    foreach ($feature in @($ManifestProfiles |
                           ForEach-Object { $_.features } |
                           ForEach-Object { $_ } |
                           Sort-Object -Unique)) {
        $sets = @($ManifestProfiles |
            Where-Object { $_.features -contains $feature } |
            ForEach-Object { $_.set } |
            Sort-Object)
        $observedSets = [System.Collections.Generic.List[string]]::new()
        foreach ($setId in $sets) {
            $rows = @($Results | Where-Object {
                [string]::Equals([string]$_.set, [string]$setId,
                                 [System.StringComparison]::OrdinalIgnoreCase)
            })
            $evidence = [System.Collections.Generic.List[object]]::new()
            foreach ($row in $rows) {
                if ($null -ne $row.runtime_evidence) {
                    $evidence.Add($row.runtime_evidence)
                }
                if ($row.gameplay_probe.enabled -and
                    $null -ne $row.gameplay_probe.runtime_evidence) {
                    $evidence.Add($row.gameplay_probe.runtime_evidence)
                }
                if ($row.audio_probe.enabled -and
                    $null -ne $row.audio_probe.runtime_evidence) {
                    $evidence.Add($row.audio_probe.runtime_evidence)
                }
                $attractAudioProbe = $row.PSObject.Properties["audio_attract_probe"]
                if ($null -ne $attractAudioProbe -and
                    $row.audio_attract_probe.enabled -and
                    $null -ne $row.audio_attract_probe.runtime_evidence) {
                    $evidence.Add($row.audio_attract_probe.runtime_evidence)
                }
            }
            if ($evidence.Count -gt 0 -and
                (Test-FeatureObserved -Feature $feature -Evidence @($evidence))) {
                $observedSets.Add([string]$setId)
            }
        }
        $hasObservationRule = $observationRules.Contains($feature)
        $runtimeRows.Add([pscustomobject]@{
            feature = $feature
            has_observation_rule = $hasObservationRule
            requires_video_evidence = $feature -in $visualFeatures -or
                                      $feature -in $videoSidecarFeatures
            requires_main_bus_evidence = $feature -in $mainBusFeatures
            requires_audio_evidence = $feature -in $audioFeatures
            requires_sound_reset_evidence = $feature -in $soundResetFeatures
            requires_watchdog_evidence = $feature -in $watchdogFeatures
            requires_board_evidence = $feature -in $boardProfileFeatures
            requires_sound_bank_evidence = $feature -in $soundBankFeatures
            requires_io_access_evidence = $feature -in $ioAccessFeatures
            requires_input_evidence = $feature -in $inputFeatures
            requires_palette_evidence = $feature -in $paletteFeatures
            evidence = if ($feature -eq "tc0200obj_control_marker_per_map") {
                "sprite_control_state_v1 + board_profile_state sidecars"
            } elseif ($feature -eq "tc0200obj_partial_buffer_byte_lane_profile") {
                "sprite_buffer_state_v1 + board_profile_state sidecars"
            } elseif ($feature -in $visualFeatures) {
                "priority_decisions_v1/decoded_sprite_objects_v1"
            } elseif ($feature -in $mainBusFeatures) {
                "main_bus_state sidecar"
            } elseif ($feature -in $ioAccessFeatures) {
                "io_access_state sidecar"
            } elseif ($feature -in $videoSidecarFeatures) {
                "video register/memory sidecars + scene timing"
            } elseif ($feature -in $boardProfileFeatures) {
                "board_profile_state sidecar"
            } elseif ($feature -in $soundBankFeatures) {
                "sound_bank_state + board_profile_state sidecars"
            } elseif ($feature -in $soundResetFeatures) {
                "sound_reset_state sidecar"
            } elseif ($feature -in $watchdogFeatures) {
                "watchdog_state sidecar"
            } elseif ($feature -in $inputFeatures) {
                "io_output_state + watchdog_state + board_profile_state sidecars"
            } elseif ($feature -in $paletteFeatures) {
                "palette_write_state + board_profile_state sidecars"
            } elseif ($feature -in $audioFeatures) {
                "rendered WAV + rendered_audio JSON + audio JSON"
            } else {
                "requires non-video board/input/audio sidecar"
            }
            sets = $sets
            observed_sets = @($observedSets)
            missing_observed_sets = @($sets | Where-Object { -not $observedSets.Contains($_) })
        })
    }
    return @($runtimeRows)
}

if ([string]::IsNullOrWhiteSpace($Rom)) {
    $Rom = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_ROM")
}
if ([string]::IsNullOrWhiteSpace($RomDir)) {
    $RomDir = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_SET_DIR")
}
if ([string]::IsNullOrWhiteSpace($ExpectedHashes)) {
    $ExpectedHashes = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_GOLDENS")
}
$requireGoldensEnv = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_REQUIRE_GOLDENS")
if (-not $RequireGoldens -and -not [string]::IsNullOrWhiteSpace($requireGoldensEnv)) {
    $RequireGoldens = $requireGoldensEnv -match '^(1|true|yes|on)$'
}
$requireManifestCoverageEnv =
    [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_REQUIRE_MANIFEST_COVERAGE")
if (-not $RequireManifestCoverage -and
    -not [string]::IsNullOrWhiteSpace($requireManifestCoverageEnv)) {
    $RequireManifestCoverage = $requireManifestCoverageEnv -match '^(1|true|yes|on)$'
}
$requireFeatureEvidenceEnv =
    [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_REQUIRE_FEATURE_EVIDENCE")
if (-not $RequireFeatureEvidence -and
    -not [string]::IsNullOrWhiteSpace($requireFeatureEvidenceEnv)) {
    $RequireFeatureEvidence = $requireFeatureEvidenceEnv -match '^(1|true|yes|on)$'
}
$gameplayProbeEnv = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_GAMEPLAY_PROBE")
if (-not $GameplayProbe -and -not [string]::IsNullOrWhiteSpace($gameplayProbeEnv)) {
    $GameplayProbe = $gameplayProbeEnv -match '^(1|true|yes|on)$'
}
$gameplayFramesEnv = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_GAMEPLAY_FRAMES")
if (-not [string]::IsNullOrWhiteSpace($gameplayFramesEnv)) {
    $GameplayFrames = [int]$gameplayFramesEnv
}
$gameplayPressEnv = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_GAMEPLAY_PRESS")
if (-not [string]::IsNullOrWhiteSpace($gameplayPressEnv)) {
    $GameplayPress = @(
        $gameplayPressEnv.Split(
            [System.IO.Path]::PathSeparator,
            [System.StringSplitOptions]::RemoveEmptyEntries))
}
$audioProbeEnv = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_AUDIO_PROBE")
if (-not $AudioProbe -and -not [string]::IsNullOrWhiteSpace($audioProbeEnv)) {
    $AudioProbe = $audioProbeEnv -match '^(1|true|yes|on)$'
}
if (-not $AudioProbe -and $RequireFeatureEvidence) {
    $AudioProbe = $true
}
$audioFramesEnv = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_AUDIO_FRAMES")
if (-not [string]::IsNullOrWhiteSpace($audioFramesEnv)) {
    $AudioFrames = [int]$audioFramesEnv
}
$audioPressEnv = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_AUDIO_PRESS")
if (-not [string]::IsNullOrWhiteSpace($audioPressEnv)) {
    $AudioPress = @(
        $audioPressEnv.Split(
            [System.IO.Path]::PathSeparator,
            [System.StringSplitOptions]::RemoveEmptyEntries))
}
$requireAudioEvidenceEnv =
    [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_REQUIRE_AUDIO_EVIDENCE")
if (-not $RequireAudioEvidence -and
    -not [string]::IsNullOrWhiteSpace($requireAudioEvidenceEnv)) {
    $RequireAudioEvidence = $requireAudioEvidenceEnv -match '^(1|true|yes|on)$'
}

$buildRoot = Resolve-RepoPath $BuildDir
$player = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "mnemos_player.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($player)) {
    throw "mnemos_player.exe not found under '$buildRoot'. Build mnemos_player first."
}

$manifestMap = Get-TaitoF2ManifestMap
$manifestProfiles = Get-TaitoF2ManifestProfiles -ManifestMap $manifestMap
$roms = [System.Collections.Generic.List[string]]::new()
Add-RomPath -Paths $roms -Path $Rom

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
            if (Test-TaitoF2ZipCandidate -Path $zip.FullName -ManifestMap $manifestMap) {
                $roms.Add($zip.FullName)
            }
        }
    } else {
        Write-Warning "Taito F2 ROM directory not found: $RomDir"
    }
}

$extra = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_EXTRA_ROMS")
if (-not [string]::IsNullOrWhiteSpace($extra)) {
    foreach ($path in $extra.Split([System.IO.Path]::PathSeparator,
                                   [System.StringSplitOptions]::RemoveEmptyEntries)) {
        Add-RomPath -Paths $roms -Path $path
    }
}

$uniqueRoms = @($roms | Sort-Object -Unique)
if ($MaxSets -gt 0) {
    $uniqueRoms = @($uniqueRoms | Select-Object -First $MaxSets)
}

if ($uniqueRoms.Count -eq 0) {
    Write-Host "No Taito F2 ROMs configured; set MNEMOS_TAITO_F2_ROM or MNEMOS_TAITO_F2_SET_DIR to run this gate." -ForegroundColor DarkGray
    exit 0
}

$expectedHashMap = Get-ExpectedHashMap -Path $ExpectedHashes

$romBySetId = [System.Collections.Generic.Dictionary[string, object]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($romPath in $uniqueRoms) {
    $candidate = Resolve-TaitoF2RomCandidate -Path $romPath -ManifestMap $manifestMap
    if ($null -ne $candidate -and -not $romBySetId.ContainsKey($candidate.SetId)) {
        $romBySetId[$candidate.SetId] = [pscustomobject]@{
            Path = $romPath
            NestedEntry = $candidate.NestedEntry
        }
    }
}

foreach ($setId in @($romBySetId.Keys)) {
    if (-not $manifestMap.ContainsKey($setId)) {
        continue
    }
    $parent = Get-ManifestParent -ManifestPath $manifestMap[$setId]
    if ($null -eq $parent -or $romBySetId.ContainsKey($parent)) {
        continue
    }
    $sibling = Find-TaitoF2SiblingSetCandidate -ChildPath $romBySetId[$setId].Path `
        -SetId $parent -ManifestMap $manifestMap
    if ($null -ne $sibling) {
        Write-Host ("[taito-f2] {0}: found sibling parent {1}" -f $setId, $parent) `
            -ForegroundColor DarkCyan
        $romBySetId[$parent] = $sibling
    }
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$outDir = Join-Path $repoRoot "build/scratch/taito-f2-corpus/$stamp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
$index = 0
foreach ($romPath in $uniqueRoms) {
    $index += 1
    $zipStem = [System.IO.Path]::GetFileNameWithoutExtension($romPath)
    $candidate = Resolve-TaitoF2RomCandidate -Path $romPath -ManifestMap $manifestMap
    if ($null -ne $candidate) {
        $candidate = [pscustomobject]@{
            SetId = $candidate.SetId
            Path = $romPath
            NestedEntry = $candidate.NestedEntry
        }
    }
    $setId = if ($null -ne $candidate) { $candidate.SetId } else { $null }
    if ($null -eq $setId) {
        $setId = $zipStem
        $candidate = [pscustomobject]@{
            Path = $romPath
            NestedEntry = $null
        }
    }
    $setOut = Join-Path $outDir ("{0:D3}-{1}" -f $index, $setId)
    New-Item -ItemType Directory -Force -Path $setOut | Out-Null

    $sourceZipPath = Get-StagedSourceZip -Candidate $candidate -OutputZip (Join-Path $setOut "$setId.source.zip")
    $runRomPath = $sourceZipPath
    $manifestPath = $null
    if ($manifestMap.ContainsKey($setId)) {
        $manifestPath = $manifestMap[$setId]
        $runRomPath = Join-Path $setOut "$setId.self.zip"
        New-ZipWithManifest -SourceZip $sourceZipPath -ManifestPath $manifestPath -OutputZip $runRomPath

        $parent = Get-ManifestParent -ManifestPath $manifestPath
        if ($null -ne $parent -and $manifestMap.ContainsKey($parent)) {
            $parentPath = Join-Path $setOut "$parent.zip"
            if ($romBySetId.ContainsKey($parent)) {
                $parentSource = Get-StagedSourceZip -Candidate $romBySetId[$parent] `
                    -OutputZip (Join-Path $setOut "$parent.source.zip")
                New-ZipWithManifest -SourceZip $parentSource `
                    -ManifestPath $manifestMap[$parent] -OutputZip $parentPath
            } else {
                Write-Warning "Taito F2 set '$setId' declares parent '$parent', but no parent zip was found in the configured corpus."
            }
        }
    }

    $statePath = Join-Path $setOut "$setId.mns"
    $saveLog = Join-Path $setOut "$setId.save.log"
    $loadLog = Join-Path $setOut "$setId.load.log"
    $screenshotPath = Join-Path $setOut "$setId.after-load.ppm"
    $gameplayLog = Join-Path $setOut "$setId.gameplay.log"
    $gameplayScreenshotPath = Join-Path $setOut "$setId.gameplay.ppm"
    $audioLog = Join-Path $setOut "$setId.audio.log"
    $audioBasePath = Join-Path $setOut "$setId.audio"
    $audioAttractLog = Join-Path $setOut "$setId.audio-attract.log"
    $audioAttractBasePath = Join-Path $setOut "$setId.audio-attract"

    Write-Host ("[taito-f2] {0}" -f $setId) -ForegroundColor Cyan

    $saveArgs = @(
        "--system", "taito_f2",
        "--rom", $runRomPath,
        "--frames", $Frames.ToString(),
        "--press", "start@1+2",
        "--press", "service@2+2",
        "--press", "test@3+2",
        "--save-state", $statePath
    )
    $saveExit = Invoke-Player -Player $player -LogPath $saveLog -Arguments $saveArgs

    $loadExit = $null
    if ($saveExit -eq 0) {
        $loadArgs = @(
            "--system", "taito_f2",
            "--rom", $runRomPath,
            "--load-state", $statePath,
            "--frames", "1",
            "--screenshot", $screenshotPath
        )
        $loadExit = Invoke-Player -Player $player -LogPath $loadLog -Arguments $loadArgs
    }

    $screenshotExists = Test-Path -LiteralPath $screenshotPath -PathType Leaf
    $frameLit = $screenshotExists -and (Test-PpmNonBlank -Path $screenshotPath)
    $screenshotHash = $null
    if ($screenshotExists) {
        $screenshotHash = (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $smokePassed = ($saveExit -eq 0 -and $loadExit -eq 0 -and
        (Test-Path -LiteralPath $statePath -PathType Leaf) -and $frameLit)
    $expectedHash = $null
    if ($expectedHashMap.ContainsKey($setId)) {
        $expectedHash = $expectedHashMap[$setId]
    }
    $hashMatches = $null
    if ($null -ne $expectedHash -and $null -ne $screenshotHash) {
        $hashMatches = [string]::Equals(
            $screenshotHash,
            $expectedHash,
            [System.StringComparison]::OrdinalIgnoreCase)
    }
    $prioritySummary = Get-PriorityDecisionSummary -ScreenshotPath $screenshotPath
    $decodedSpriteSummary = Get-DecodedSpriteObjectSummary -ScreenshotPath $screenshotPath
    $runtimeEvidence = Get-RuntimeFeatureEvidence -PrioritySummary $prioritySummary `
        -DecodedSpriteSummary $decodedSpriteSummary `
        -ScreenshotPath $screenshotPath
    $gameplayExit = $null
    $gameplayFrameLit = $false
    $gameplayHash = $null
    $gameplayPrioritySummary = [pscustomobject]@{
        present = $false
        path = $null
        bytes = 0
        valid = $false
        records = 0
        final_sources = [pscustomobject]@{}
        rejected_sources = [pscustomobject]@{}
        last_rejected_sources = [pscustomobject]@{}
        sprite_occupied_pixels = 0
        sprite_priority_reject_pixels = 0
        sprite_occupancy_reject_pixels = 0
        layer_priority_reject_pixels = 0
    }
    $gameplayDecodedSpriteSummary = [pscustomobject]@{
        present = $false
        path = $null
        bytes = 0
        valid = $false
        records = 0
    }
    $gameplayRuntimeEvidence = Get-RuntimeFeatureEvidence `
        -PrioritySummary $gameplayPrioritySummary `
        -DecodedSpriteSummary $gameplayDecodedSpriteSummary
    if ($GameplayProbe) {
        $gameplayArgs = @(
            "--system", "taito_f2",
            "--rom", $runRomPath,
            "--frames", $GameplayFrames.ToString(),
            "--screenshot", $gameplayScreenshotPath
        )
        foreach ($press in $GameplayPress) {
            if (-not [string]::IsNullOrWhiteSpace($press)) {
                $gameplayArgs += @("--press", $press)
            }
        }
        $gameplayExit = Invoke-Player -Player $player -LogPath $gameplayLog -Arguments $gameplayArgs
        if (Test-Path -LiteralPath $gameplayScreenshotPath -PathType Leaf) {
            $gameplayFrameLit = Test-PpmNonBlank -Path $gameplayScreenshotPath
            $gameplayHash =
                (Get-FileHash -LiteralPath $gameplayScreenshotPath -Algorithm SHA256).
                    Hash.ToLowerInvariant()
            $gameplayPrioritySummary =
                Get-PriorityDecisionSummary -ScreenshotPath $gameplayScreenshotPath
            $gameplayDecodedSpriteSummary =
                Get-DecodedSpriteObjectSummary -ScreenshotPath $gameplayScreenshotPath
            $gameplayRuntimeEvidence = Get-RuntimeFeatureEvidence `
                -PrioritySummary $gameplayPrioritySummary `
                -DecodedSpriteSummary $gameplayDecodedSpriteSummary `
                -ScreenshotPath $gameplayScreenshotPath
        }
    }
    $audioExit = $null
    $audioSummary = New-MissingAudioSummary -BasePath $audioBasePath
    $audioRuntimeEvidence = Get-AudioRuntimeEvidence -AudioSummary $audioSummary
    $audioAttractExit = $null
    $audioAttractFrames = [Math]::Max($AudioFrames, 12000)
    $audioAttractSummary = New-MissingAudioSummary -BasePath $audioAttractBasePath
    $audioAttractRuntimeEvidence = Get-AudioRuntimeEvidence -AudioSummary $audioAttractSummary
    $audioAttractProbeEnabled = $false
    if ($AudioProbe) {
        $audioArgs = @(
            "--system", "taito_f2",
            "--rom", $runRomPath,
            "--extract-frames", $AudioFrames.ToString(),
            "--extract-audio", $audioBasePath
        )
        foreach ($press in $AudioPress) {
            if (-not [string]::IsNullOrWhiteSpace($press)) {
                $audioArgs += @("--press", $press)
            }
        }
        $audioExit = Invoke-Player -Player $player -LogPath $audioLog -Arguments $audioArgs
        $audioSummary = Get-RenderedAudioSummary -BasePath $audioBasePath
        $audioRuntimeEvidence = Get-AudioRuntimeEvidence -AudioSummary $audioSummary

        $manifestHasAdpcma = $null -ne $manifestPath -and
            (Test-TomlRegion -ManifestPath $manifestPath -Name "adpcma")
        $manifestHasAdpcmb = $null -ne $manifestPath -and
            (Test-TomlRegion -ManifestPath $manifestPath -Name "adpcmb")
        $needsAdpcmaAttractProbe =
            $RequireFeatureEvidence -and
            $manifestHasAdpcma -and
            ((-not (Test-EvidenceFlag -Evidence $audioRuntimeEvidence -Name "adpcma_active")) -or
             (-not (Test-EvidenceFlag -Evidence $audioRuntimeEvidence -Name "adpcma_rekey_trace")) -or
             (Get-EvidenceInt -Evidence $audioRuntimeEvidence -Name "adpcma_key_on_writes") -eq 0)
        $needsAdpcmbAttractProbe =
            $RequireFeatureEvidence -and
            $manifestHasAdpcmb -and
            ((-not (Test-EvidenceFlag -Evidence $audioRuntimeEvidence -Name "adpcmb_active")) -or
             (Get-EvidenceInt -Evidence $audioRuntimeEvidence -Name "adpcmb_start_writes") -eq 0)
        if ($needsAdpcmaAttractProbe -or $needsAdpcmbAttractProbe) {
            $audioAttractProbeEnabled = $true
            $audioAttractArgs = @(
                "--system", "taito_f2",
                "--rom", $runRomPath,
                "--extract-frames", $audioAttractFrames.ToString(),
                "--extract-audio", $audioAttractBasePath
            )
            $audioAttractExit = Invoke-Player -Player $player `
                -LogPath $audioAttractLog -Arguments $audioAttractArgs
            $audioAttractSummary = Get-RenderedAudioSummary -BasePath $audioAttractBasePath
            $audioAttractRuntimeEvidence =
                Get-AudioRuntimeEvidence -AudioSummary $audioAttractSummary
        }
    }
    $goldenPassed = $true
    if ($null -ne $expectedHash) {
        $goldenPassed = ($hashMatches -eq $true)
    } elseif ($RequireGoldens) {
        $goldenPassed = $false
    }
    $audioPassed = (-not $RequireAudioEvidence) -or
        ($AudioProbe -and $audioExit -eq 0 -and $audioSummary.valid -and
         [int]$audioSummary.nonzero_frames -gt 0 -and $audioSummary.trace_valid)
    $passed = $smokePassed -and $goldenPassed -and $audioPassed
    $results.Add([pscustomobject]@{
        set = $setId
        rom = $romPath
        manifest = $manifestPath
        synthesized_rom = if ($runRomPath -eq $romPath) { $null } else { $runRomPath }
        save_exit = $saveExit
        load_exit = $loadExit
        frame_lit = $frameLit
        screenshot_sha256 = $screenshotHash
        expected_screenshot_sha256 = $expectedHash
        screenshot_hash_matches = $hashMatches
        golden_required = [bool]$RequireGoldens
        passed = $passed
        state = $statePath
        screenshot = $screenshotPath
        priority_decisions = $prioritySummary
        decoded_sprite_objects = $decodedSpriteSummary
        runtime_evidence = $runtimeEvidence
        gameplay_probe = [pscustomobject]@{
            enabled = [bool]$GameplayProbe
            frames = $GameplayFrames
            presses = @($GameplayPress)
            exit = $gameplayExit
            frame_lit = $gameplayFrameLit
            screenshot_sha256 = $gameplayHash
            screenshot = $gameplayScreenshotPath
            priority_decisions = $gameplayPrioritySummary
            decoded_sprite_objects = $gameplayDecodedSpriteSummary
            runtime_evidence = $gameplayRuntimeEvidence
            log = $gameplayLog
        }
        audio_probe = [pscustomobject]@{
            enabled = [bool]$AudioProbe
            frames = $AudioFrames
            presses = @($AudioPress)
            exit = $audioExit
            required = [bool]$RequireAudioEvidence
            passed = $audioPassed
            summary = $audioSummary
            runtime_evidence = $audioRuntimeEvidence
            base = $audioBasePath
            log = $audioLog
        }
        audio_attract_probe = [pscustomobject]@{
            enabled = [bool]$audioAttractProbeEnabled
            frames = $audioAttractFrames
            presses = @()
            exit = $audioAttractExit
            summary = $audioAttractSummary
            runtime_evidence = $audioAttractRuntimeEvidence
            base = $audioAttractBasePath
            log = $audioAttractLog
        }
        save_log = $saveLog
        load_log = $loadLog
    })
}

$summaryPath = Join-Path $outDir "summary.json"
$coveragePath = Join-Path $outDir "manifest-coverage.json"
$manifestCoverage = Get-TaitoF2ManifestCoverage -ManifestProfiles $manifestProfiles `
    -Results @($results)
$runtimeFeatureCoverage = Get-TaitoF2RuntimeFeatureCoverage `
    -ManifestProfiles $manifestProfiles -Results @($results)
$knownRosterDebt = Get-TaitoF2KnownRosterDebt -ManifestProfiles $manifestProfiles
$preRuntimeAuditDebt = Get-TaitoF2PreRuntimeAuditDebt `
    -ManifestProfiles $manifestProfiles -RuntimeFeatureCoverage $runtimeFeatureCoverage
$manifestCoverage | Add-Member -NotePropertyName runtime_feature_gates `
    -NotePropertyValue @($runtimeFeatureCoverage)
$manifestCoverage | Add-Member -NotePropertyName known_f2_roster_debt `
    -NotePropertyValue $knownRosterDebt
$manifestCoverage | Add-Member -NotePropertyName pre_runtime_audit_debt `
    -NotePropertyValue $preRuntimeAuditDebt
$results | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryPath -Encoding utf8
$manifestCoverage | ConvertTo-Json -Depth 8 |
    Set-Content -Path $coveragePath -Encoding utf8

$failed = @($results | Where-Object { -not $_.passed })
$missingManifestSets = @($manifestCoverage.missing_sets)
Write-Host ("Taito F2 corpus smoke: {0}/{1} passed; summary: {2}" -f ($results.Count - $failed.Count), $results.Count, $summaryPath)
Write-Host ("Taito F2 manifest coverage: {0}/{1} checked; details: {2}" -f @($manifestCoverage.attempted_sets).Count, $manifestCoverage.total_manifest_sets, $coveragePath)
if (@($knownRosterDebt.missing_sets).Count -gt 0) {
    Write-Host ("Taito F2 known-roster debt: {0} sets not manifest-backed yet: {1}" -f `
        @($knownRosterDebt.missing_sets).Count,
        (@($knownRosterDebt.missing_sets) -join ", ")) -ForegroundColor Yellow
}
if (@($preRuntimeAuditDebt.unmapped_runtime_feature_gates).Count -gt 0) {
    Write-Host ("Taito F2 feature-observation debt: {0} named gates have no observation rule yet." -f `
        @($preRuntimeAuditDebt.unmapped_runtime_feature_gates).Count) -ForegroundColor Yellow
}
if (@($preRuntimeAuditDebt.tc0260dar_profile_mismatches).Count -gt 0) {
    Write-Host ("Taito F2 TC0260DAR profile debt: {0} manifests still use another palette profile." -f `
        @($preRuntimeAuditDebt.tc0260dar_profile_mismatches).Count) -ForegroundColor Yellow
}
$attemptedSetNames = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($setId in @($manifestCoverage.attempted_sets)) {
    if (-not [string]::IsNullOrWhiteSpace([string]$setId)) {
        [void]$attemptedSetNames.Add([string]$setId)
    }
}
$featureEvidenceMissing = @($runtimeFeatureCoverage | Where-Object {
    if (-not $_.has_observation_rule) {
        return $false
    }
    $requiresEvidence =
        $_.requires_video_evidence -or
        $_.requires_main_bus_evidence -or
        $_.requires_audio_evidence -or
        $_.requires_sound_reset_evidence -or
        $_.requires_watchdog_evidence -or
        $_.requires_board_evidence -or
        $_.requires_sound_bank_evidence -or
        $_.requires_io_access_evidence -or
        $_.requires_input_evidence -or
        $_.requires_palette_evidence
    if (-not $requiresEvidence) {
        return $false
    }
    $missing = if ($RequireManifestCoverage) {
        @($_.missing_observed_sets)
    } else {
        @($_.missing_observed_sets |
            Where-Object { $attemptedSetNames.Contains([string]$_) })
    }
    @($missing).Count -gt 0
})
if ($failed.Count -gt 0) {
    foreach ($row in $failed) {
        $goldenState = "unconfigured"
        if ($null -eq $row.expected_screenshot_sha256 -and $row.golden_required) {
            $goldenState = "missing"
        } elseif ($null -ne $row.expected_screenshot_sha256 -and $row.screenshot_hash_matches) {
            $goldenState = "matched"
        } elseif ($null -ne $row.expected_screenshot_sha256) {
            $goldenState = "mismatch"
        }
        $audioState = "not-required"
        if ($row.audio_probe.required) {
            if (-not $row.audio_probe.enabled) {
                $audioState = "not-run"
            } elseif ($row.audio_probe.exit -ne 0) {
                $audioState = "exit-$($row.audio_probe.exit)"
            } elseif (-not $row.audio_probe.summary.valid) {
                $audioState = "invalid"
            } elseif ([int]$row.audio_probe.summary.nonzero_frames -le 0) {
                $audioState = "silent"
            } else {
                $audioState = "nonzero"
            }
        }
        Write-Host ("  [fail] {0} save={1} load={2} lit={3} golden={4} audio={5}" -f $row.set, $row.save_exit, $row.load_exit, $row.frame_lit, $goldenState, $audioState) -ForegroundColor Red
    }
    exit 1
}
if ($RequireManifestCoverage -and $missingManifestSets.Count -gt 0) {
    Write-Host ("  [fail] manifest coverage missing {0}: {1}" -f `
        $missingManifestSets.Count, ($missingManifestSets -join ", ")) -ForegroundColor Red
    exit 1
}
if ($RequireFeatureEvidence -and $featureEvidenceMissing.Count -gt 0) {
    foreach ($gate in $featureEvidenceMissing) {
        $missing = if ($RequireManifestCoverage) {
            @($gate.missing_observed_sets)
        } else {
            @($gate.missing_observed_sets |
                Where-Object { $attemptedSetNames.Contains([string]$_) })
        }
        Write-Host ("  [fail] runtime feature evidence missing {0}: {1}" -f `
            $gate.feature, ($missing -join ", ")) -ForegroundColor Red
    }
    exit 1
}

exit 0
