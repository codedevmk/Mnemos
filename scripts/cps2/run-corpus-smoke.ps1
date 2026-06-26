#!/usr/bin/env pwsh
# Data-gated CPS2 corpus smoke runner.
#
# ROM/key zips are never committed. Point this at a single zip with
# MNEMOS_CPS2_ROM, or at a directory of zips with MNEMOS_CPS2_SET_DIR.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string]$Rom = "",
    [string]$RomDir = "",
    [string]$ExpectedFrameHashes = "",
    [int]$Frames = 600,
    [int]$AudioFrames = 0,
    [int]$AudioSignificantThreshold = 64,
    [switch]$AudioStateProbe,
    [int]$MaxSets = 0,
    [string[]]$OnlySets = @(),
    [string[]]$SkipSets = @(),
    [string]$StartAfter = "",
    [switch]$IncludeAllZips,
    [switch]$GameplayInput,
    [ValidateRange(1, 4)]
    [int]$GameplayPlayers = 4,
    [switch]$GameplayRepeat,
    [switch]$GameplaySaveInput,
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

function Get-Cps2SaveFrameCount {
    param(
        [Parameter(Mandatory = $true)][string]$SetId,
        [Parameter(Mandatory = $true)][int]$DefaultFrames
    )

    # Armored Warriors is still black at the default 600-frame checkpoint in both
    # Mnemos and the older Emu CPS2 probe; 700 frames reaches the first lit attract
    # frame without changing the rest of the smoke gate.
    if ($SetId -eq "armwar" -and $DefaultFrames -lt 700) {
        return 700
    }

    # Darkstalkers self-initializes EEPROM/QSound state and remains black past
    # the 600-frame gate; 1200 frames reaches the first stable visible attract frame.
    if ($SetId -eq "dstlk" -and $DefaultFrames -lt 1200) {
        return 1200
    }
    return $DefaultFrames
}

function Get-Cps2AudioFrameCount {
    param(
        [Parameter(Mandatory = $true)][string]$SetId,
        [Parameter(Mandatory = $true)][int]$DefaultFrames
    )

    # These rows keep the save/load screenshot checkpoint at 600 frames while
    # using a longer repeated-gameplay audio window that proves nonzero QSound
    # output plus audio-state register counters.
    if (($SetId -eq "batcir" -or $SetId -eq "ddsom") -and $DefaultFrames -lt 2500) {
        return 2500
    }

    # HSF2's first significant QSound output lands after frame 1198. Keep the
    # screenshot/save-state gate at 600 frames, but preserve the focused
    # HSF2-vs-Emu oracle window in the committed audio row.
    if ($SetId -eq "hsf2" -and $DefaultFrames -lt 6040) {
        return 6040
    }
    return $DefaultFrames
}

function Test-Cps2AudioStateProbeDefault {
    param([Parameter(Mandatory = $true)][string]$SetId)

    return $SetId -eq "batcir" -or $SetId -eq "ddsom" -or $SetId -eq "hsf2" -or
        $SetId -eq "mshvsf" -or $SetId -eq "mvsc"
}

function Test-Cps2AudioGameplayProbeDefault {
    param([Parameter(Mandatory = $true)][string]$SetId)

    return $SetId -eq "batcir" -or $SetId -eq "ddsom"
}

function Get-Cps2AudioGameplayPlayerCount {
    param(
        [Parameter(Mandatory = $true)][string]$SetId,
        [Parameter(Mandatory = $true)][int]$DefaultPlayers
    )

    if ($SetId -eq "batcir" -or $SetId -eq "ddsom") {
        return 4
    }
    return $DefaultPlayers
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

function Get-Cps2CorpusHashType {
    $type = "Mnemos.Cps2CorpusHash" -as [type]
    if ($null -ne $type) {
        return $type
    }

    Add-Type -TypeDefinition @'
using System;
using System.Globalization;

namespace Mnemos {
    public static class Cps2CorpusHash {
        public static string Fnv1a64Bytes(byte[] bytes, int offset, int count) {
            if (bytes == null) {
                throw new ArgumentNullException(nameof(bytes));
            }
            if (offset < 0 || count < 0 || offset > bytes.Length || bytes.Length - offset < count) {
                throw new ArgumentOutOfRangeException(nameof(count));
            }

            unchecked {
                ulong hash = 1469598103934665603UL;
                const ulong prime = 1099511628211UL;
                for (int i = 0; i < count; ++i) {
                    hash ^= bytes[offset + i];
                    hash *= prime;
                }
                return hash.ToString("X16", CultureInfo.InvariantCulture);
            }
        }
    }
}
'@
    return ("Mnemos.Cps2CorpusHash" -as [type])
}

function Skip-PpmHeaderWhitespace {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][ref]$Offset
    )
    while ($Offset.Value -lt $Bytes.Length) {
        $byte = $Bytes[$Offset.Value]
        if ($byte -eq [byte][char]'#') {
            while ($Offset.Value -lt $Bytes.Length -and
                $Bytes[$Offset.Value] -ne [byte][char]"`n") {
                $Offset.Value += 1
            }
            continue
        }
        if (-not [char]::IsWhiteSpace([char]$byte)) {
            break
        }
        $Offset.Value += 1
    }
}

function Read-PpmHeaderToken {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][ref]$Offset
    )
    Skip-PpmHeaderWhitespace -Bytes $Bytes -Offset $Offset
    $start = $Offset.Value
    while ($Offset.Value -lt $Bytes.Length -and
        -not [char]::IsWhiteSpace([char]$Bytes[$Offset.Value])) {
        if ($Bytes[$Offset.Value] -eq [byte][char]'#') {
            break
        }
        $Offset.Value += 1
    }
    if ($Offset.Value -eq $start) {
        throw "Malformed PPM header in screenshot."
    }
    return [System.Text.Encoding]::ASCII.GetString($Bytes, $start, $Offset.Value - $start)
}

function Get-PpmFrameProbe {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $offset = 0
    $magic = Read-PpmHeaderToken -Bytes $bytes -Offset ([ref]$offset)
    if ($magic -ne "P6") {
        throw "Unsupported screenshot format '$magic'; expected binary P6 PPM."
    }
    $width = [int](Read-PpmHeaderToken -Bytes $bytes -Offset ([ref]$offset))
    $height = [int](Read-PpmHeaderToken -Bytes $bytes -Offset ([ref]$offset))
    $maxValue = [int](Read-PpmHeaderToken -Bytes $bytes -Offset ([ref]$offset))
    if ($width -le 0 -or $height -le 0 -or $maxValue -ne 255) {
        throw "Unsupported PPM dimensions or max value in screenshot."
    }
    if ($offset -ge $bytes.Length -or -not [char]::IsWhiteSpace([char]$bytes[$offset])) {
        throw "Malformed PPM pixel-data separator."
    }
    $offset += 1

    $rgbByteCount = $width * $height * 3
    if ($bytes.Length -lt ($offset + $rgbByteCount)) {
        throw "Truncated PPM pixel data."
    }

    $nonzeroPixels = 0
    for ($i = 0; $i -lt $rgbByteCount; $i += 3) {
        $pixelOffset = $offset + $i
        if ($bytes[$pixelOffset] -ne 0 -or
            $bytes[$pixelOffset + 1] -ne 0 -or
            $bytes[$pixelOffset + 2] -ne 0) {
            $nonzeroPixels += 1
        }
    }

    $hashType = Get-Cps2CorpusHashType
    $hash = [string]$hashType.GetMethod("Fnv1a64Bytes").Invoke($null, @($bytes, $offset, $rgbByteCount))
    return [pscustomobject]@{
        frame_width = $width
        frame_height = $height
        frame_rgb_format = "RGB24"
        frame_rgb_hash_algorithm = "fnv1a64-rgb24"
        frame_rgb_hash = $hash
        frame_nonzero_rgb_pixels = $nonzeroPixels
        frame_rgb_bytes = $rgbByteCount
    }
}

function Read-LeUInt16 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    return [uint16]($Bytes[$Offset] -bor ($Bytes[($Offset + 1)] -shl 8))
}

function Read-LeUInt32 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    $b0 = [uint32]$Bytes[$Offset]
    $b1 = ([uint32]$Bytes[($Offset + 1)]) -shl 8
    $b2 = ([uint32]$Bytes[($Offset + 2)]) -shl 16
    $b3 = ([uint32]$Bytes[($Offset + 3)]) -shl 24
    return [uint32]($b0 -bor $b1 -bor $b2 -bor $b3)
}

function Read-LeInt16 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    return [BitConverter]::ToInt16($Bytes, $Offset)
}

function Get-WavAudioProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$SignificantThreshold
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 44) {
        throw "Truncated WAV file."
    }
    if ([System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "RIFF" -or
        [System.Text.Encoding]::ASCII.GetString($bytes, 8, 4) -ne "WAVE") {
        throw "Unsupported audio format; expected RIFF/WAVE."
    }

    $offset = 12
    $formatTag = 0
    $channels = 0
    $sampleRate = 0
    $bitsPerSample = 0
    $dataOffset = -1
    $dataBytes = 0
    while ($offset + 8 -le $bytes.Length) {
        $chunkId = [System.Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $chunkSize = [int](Read-LeUInt32 -Bytes $bytes -Offset ($offset + 4))
        $payload = $offset + 8
        if ($chunkSize -lt 0 -or $payload + $chunkSize -gt $bytes.Length) {
            throw "Malformed WAV chunk '$chunkId'."
        }
        if ($chunkId -eq "fmt ") {
            if ($chunkSize -lt 16) {
                throw "Malformed WAV fmt chunk."
            }
            $formatTag = Read-LeUInt16 -Bytes $bytes -Offset $payload
            $channels = Read-LeUInt16 -Bytes $bytes -Offset ($payload + 2)
            $sampleRate = Read-LeUInt32 -Bytes $bytes -Offset ($payload + 4)
            $bitsPerSample = Read-LeUInt16 -Bytes $bytes -Offset ($payload + 14)
        } elseif ($chunkId -eq "data") {
            $dataOffset = $payload
            $dataBytes = $chunkSize
        }
        $offset = $payload + $chunkSize
        if (($offset % 2) -ne 0) {
            $offset += 1
        }
    }

    if ($formatTag -ne 1 -or $channels -le 0 -or $sampleRate -eq 0 -or $bitsPerSample -ne 16) {
        throw "Unsupported WAV encoding; expected 16-bit PCM."
    }
    if ($dataOffset -lt 0) {
        throw "WAV data chunk missing."
    }
    $blockAlign = $channels * 2
    if (($dataBytes % $blockAlign) -ne 0) {
        throw "WAV data chunk is not frame-aligned."
    }

    $nonzero = 0
    $peak = 0
    $firstNonzeroSample = -1
    $firstNonzeroFrame = -1
    $lastNonzeroSample = -1
    $lastNonzeroFrame = -1
    $significantSamples = 0
    $firstSignificantSample = -1
    $firstSignificantFrame = -1
    $lastSignificantSample = -1
    $lastSignificantFrame = -1
    for ($i = 0; $i -lt $dataBytes; $i += 2) {
        $sample = Read-LeInt16 -Bytes $bytes -Offset ($dataOffset + $i)
        if ($sample -ne 0) {
            if ($firstNonzeroSample -lt 0) {
                $firstNonzeroSample = [int]($i / 2)
                $firstNonzeroFrame = [int]([Math]::Floor($firstNonzeroSample / $channels))
            }
            $lastNonzeroSample = [int]($i / 2)
            $lastNonzeroFrame = [int]([Math]::Floor($lastNonzeroSample / $channels))
            $nonzero += 1
        }
        $abs = [Math]::Abs([int]$sample)
        if ($abs -ge $SignificantThreshold) {
            if ($firstSignificantSample -lt 0) {
                $firstSignificantSample = [int]($i / 2)
                $firstSignificantFrame = [int]([Math]::Floor($firstSignificantSample / $channels))
            }
            $lastSignificantSample = [int]($i / 2)
            $lastSignificantFrame = [int]([Math]::Floor($lastSignificantSample / $channels))
            $significantSamples += 1
        }
        if ($abs -gt $peak) {
            $peak = $abs
        }
    }

    $hashType = Get-Cps2CorpusHashType
    $hash = [string]$hashType.GetMethod("Fnv1a64Bytes").Invoke($null, @($bytes, $dataOffset, $dataBytes))
    return [pscustomobject]@{
        audio_sample_rate = $sampleRate
        audio_channels = $channels
        audio_bits_per_sample = $bitsPerSample
        audio_frame_count = [int]($dataBytes / $blockAlign)
        audio_pcm_hash_algorithm = "fnv1a64-pcm16le"
        audio_pcm_hash = $hash
        audio_nonzero_samples = $nonzero
        audio_peak_abs = $peak
        audio_first_nonzero_sample = $firstNonzeroSample
        audio_first_nonzero_frame = $firstNonzeroFrame
        audio_last_nonzero_sample = $lastNonzeroSample
        audio_last_nonzero_frame = $lastNonzeroFrame
        audio_significant_threshold = $SignificantThreshold
        audio_significant_samples = $significantSamples
        audio_first_significant_sample = $firstSignificantSample
        audio_first_significant_frame = $firstSignificantFrame
        audio_last_significant_sample = $lastSignificantSample
        audio_last_significant_frame = $lastSignificantFrame
        audio_data_bytes = $dataBytes
    }
}

function Get-BinaryProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Role = "binary"
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $nonFF = 0
    foreach ($byte in $bytes) {
        if ($byte -ne 0xFF) {
            $nonFF += 1
        }
    }

    $hashType = Get-Cps2CorpusHashType
    $hash = [string]$hashType.GetMethod("Fnv1a64Bytes").Invoke($null, @($bytes, 0, $bytes.Length))
    return [pscustomobject]@{
        battery_role = $Role
        battery_bytes = $bytes.Length
        battery_hash_algorithm = "fnv1a64-bytes"
        battery_hash = $hash
        battery_non_ff_bytes = $nonFF
    }
}

function Get-QSoundRegisterProbe {
    param([Parameter(Mandatory = $true)][string]$Path)

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([^\s#]+)\s+bits=\d+\s+format=\S+\s+value=0x([0-9A-Fa-f]+)') {
            $values[$matches[1]] = [Convert]::ToUInt64($matches[2], 16)
        }
    }

    $portWrites = if ($values.ContainsKey("PORTWR")) { $values["PORTWR"] } else { 0 }
    $registerWrites = if ($values.ContainsKey("REGWR")) { $values["REGWR"] } else { 0 }
    $lastReg = if ($values.ContainsKey("LASTREG")) { "{0:X2}" -f $values["LASTREG"] } else { "" }
    $lastData = if ($values.ContainsKey("LASTDATA")) { "{0:X4}" -f $values["LASTDATA"] } else { "" }
    $lastPc = if ($values.ContainsKey("LASTPC")) { "{0:X4}" -f $values["LASTPC"] } else { "" }
    $pcmVolumeWrites = if ($values.ContainsKey("PCM_VOLWR")) { $values["PCM_VOLWR"] } else { 0 }
    $adpcmVolumeWrites = if ($values.ContainsKey("ADPCM_VOLWR")) { $values["ADPCM_VOLWR"] } else { 0 }
    $adpcmTriggers = if ($values.ContainsKey("ADPCM_TRIG")) { $values["ADPCM_TRIG"] } else { 0 }
    $programmedAudioCommands = $pcmVolumeWrites + $adpcmVolumeWrites + $adpcmTriggers
    $adpcmConfiguredVoices = 0
    $adpcmFlaggedVoices = 0
    $adpcmNonzeroVolumeVoices = 0
    $adpcmNonzeroRangeVoices = 0
    $adpcmTriggerOnlyVoices = 0
    for ($voice = 0; $voice -lt 3; ++$voice) {
        $prefix = "ADPCM${voice}_"
        $start = if ($values.ContainsKey("${prefix}START")) { $values["${prefix}START"] } else { 0 }
        $end = if ($values.ContainsKey("${prefix}END")) { $values["${prefix}END"] } else { 0 }
        $volume = if ($values.ContainsKey("${prefix}VOL")) { $values["${prefix}VOL"] } else { 0 }
        $play = if ($values.ContainsKey("${prefix}PLAY")) { $values["${prefix}PLAY"] } else { 0 }
        $flag = if ($values.ContainsKey("${prefix}FLAG")) { $values["${prefix}FLAG"] } else { 0 }
        if ($start -ne 0 -or $end -ne 0 -or $volume -ne 0 -or $play -ne 0 -or $flag -ne 0) {
            ++$adpcmConfiguredVoices
        }
        if ($flag -ne 0) {
            ++$adpcmFlaggedVoices
        }
        if ($volume -ne 0 -or $play -ne 0) {
            ++$adpcmNonzeroVolumeVoices
        }
        if ($start -ne $end) {
            ++$adpcmNonzeroRangeVoices
        }
        if ($flag -ne 0 -and $volume -eq 0 -and $play -eq 0 -and $start -eq 0 -and $end -eq 0) {
            ++$adpcmTriggerOnlyVoices
        }
    }

    return [pscustomobject]@{
        qsound_probe_role = "dl_1425_regs"
        qsound_probe_present = 1
        qsound_port_writes = $portWrites
        qsound_register_writes = $registerWrites
        qsound_last_reg = $lastReg
        qsound_last_data = $lastData
        qsound_last_pc = $lastPc
        qsound_pcm_volume_writes = $pcmVolumeWrites
        qsound_adpcm_volume_writes = $adpcmVolumeWrites
        qsound_adpcm_triggers = $adpcmTriggers
        qsound_adpcm_configured_voices = $adpcmConfiguredVoices
        qsound_adpcm_flagged_voices = $adpcmFlaggedVoices
        qsound_adpcm_nonzero_volume_voices = $adpcmNonzeroVolumeVoices
        qsound_adpcm_nonzero_range_voices = $adpcmNonzeroRangeVoices
        qsound_adpcm_trigger_only_voices = $adpcmTriggerOnlyVoices
        qsound_programmed_audio_commands = $programmedAudioCommands
    }
}

function Test-CsvProperty {
    param(
        [Parameter(Mandatory = $true)][object]$Row,
        [Parameter(Mandatory = $true)][string]$Name
    )
    return $Row.PSObject.Properties.Name -contains $Name
}

function Get-CsvValue {
    param(
        [Parameter(Mandatory = $true)][object]$Row,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not (Test-CsvProperty -Row $Row -Name $Name)) {
        return ""
    }
    return [string]$Row.$Name
}

function Compare-FrameHashRows {
    param(
        [Parameter(Mandatory = $true)][object[]]$CurrentRows,
        [Parameter(Mandatory = $true)][string]$ExpectedCsvPath,
        [Parameter(Mandatory = $true)][bool]$RequireAllExpectedRows
    )

    $expectedRows = @(Import-Csv -LiteralPath $ExpectedCsvPath)
    $expectedByZip = @{}
    $seenZips = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $mismatches = @()
    $fields = @(
        "frames",
        "frame_width",
        "frame_height",
        "frame_rgb_format",
        "frame_rgb_hash_algorithm",
        "frame_rgb_hash",
        "frame_nonzero_rgb_pixels",
        "audio_frames",
        "audio_sample_rate",
        "audio_channels",
        "audio_bits_per_sample",
        "audio_frame_count",
        "audio_pcm_hash_algorithm",
        "audio_pcm_hash",
        "audio_nonzero_samples",
        "audio_peak_abs",
        "audio_first_nonzero_sample",
        "audio_first_nonzero_frame",
        "audio_last_nonzero_sample",
        "audio_last_nonzero_frame",
        "audio_significant_threshold",
        "audio_significant_samples",
        "audio_first_significant_sample",
        "audio_first_significant_frame",
        "audio_last_significant_sample",
        "audio_last_significant_frame",
        "qsound_probe_role",
        "qsound_probe_present",
        "qsound_port_writes",
        "qsound_register_writes",
        "qsound_last_reg",
        "qsound_last_data",
        "qsound_last_pc",
        "qsound_pcm_volume_writes",
        "qsound_adpcm_volume_writes",
        "qsound_adpcm_triggers",
        "qsound_adpcm_configured_voices",
        "qsound_adpcm_flagged_voices",
        "qsound_adpcm_nonzero_volume_voices",
        "qsound_adpcm_nonzero_range_voices",
        "qsound_adpcm_trigger_only_voices",
        "qsound_programmed_audio_commands",
        "qsound_programmed_silent",
        "audio_qsound_probe_present",
        "audio_qsound_port_writes",
        "audio_qsound_register_writes",
        "audio_qsound_last_reg",
        "audio_qsound_last_data",
        "audio_qsound_last_pc",
        "audio_qsound_pcm_volume_writes",
        "audio_qsound_adpcm_volume_writes",
        "audio_qsound_adpcm_triggers",
        "audio_qsound_adpcm_configured_voices",
        "audio_qsound_adpcm_flagged_voices",
        "audio_qsound_adpcm_nonzero_volume_voices",
        "audio_qsound_adpcm_nonzero_range_voices",
        "audio_qsound_adpcm_trigger_only_voices",
        "audio_qsound_programmed_audio_commands",
        "battery_role",
        "battery_bytes",
        "battery_hash_algorithm",
        "battery_hash",
        "battery_non_ff_bytes"
    )

    foreach ($expected in $expectedRows) {
        $zip = Get-CsvValue -Row $expected -Name "zip"
        if ($zip -eq "") {
            continue
        }
        if (-not $expectedByZip.ContainsKey($zip)) {
            $expectedByZip[$zip] = $expected
        }
    }

    foreach ($current in $CurrentRows) {
        $zip = Get-CsvValue -Row $current -Name "zip"
        if ($zip -eq "") {
            continue
        }
        [void]$seenZips.Add($zip)
        if (-not $expectedByZip.ContainsKey($zip)) {
            $mismatches += [pscustomobject]@{
                zip = $zip
                game = Get-CsvValue -Row $current -Name "game"
                field = "<row>"
                expected = "<present in baseline>"
                actual = "<missing>"
            }
            continue
        }

        $expected = $expectedByZip[$zip]
        foreach ($field in $fields) {
            if (-not (Test-CsvProperty -Row $expected -Name $field)) {
                continue
            }
            $expectedValue = (Get-CsvValue -Row $expected -Name $field).Trim()
            if ($expectedValue -eq "") {
                continue
            }
            $actualValue = (Get-CsvValue -Row $current -Name $field).Trim()
            if ($field -like "*hash*") {
                $expectedValue = $expectedValue.ToUpperInvariant()
                $actualValue = $actualValue.ToUpperInvariant()
            }
            if ($expectedValue -ne $actualValue) {
                $mismatches += [pscustomobject]@{
                    zip = $zip
                    game = Get-CsvValue -Row $current -Name "game"
                    field = $field
                    expected = $expectedValue
                    actual = $actualValue
                }
            }
        }
    }

    if ($RequireAllExpectedRows) {
        foreach ($zip in $expectedByZip.Keys) {
            if (-not $seenZips.Contains($zip)) {
                $mismatches += [pscustomobject]@{
                    zip = $zip
                    game = Get-CsvValue -Row $expectedByZip[$zip] -Name "game"
                    field = "<row>"
                    expected = "<present in current corpus>"
                    actual = "<missing>"
                }
            }
        }
    }

    return @($mismatches)
}

function Get-Cps2PressArguments {
    param(
        [Parameter(Mandatory = $true)][int]$FrameCount,
        [Parameter(Mandatory = $true)][bool]$UseGameplayInput,
        [Parameter(Mandatory = $true)][int]$GameplayPlayers,
        [Parameter(Mandatory = $true)][bool]$UseGameplayRepeat
    )

    if (-not $UseGameplayInput) {
        return @(
            "--press", "start@1+2",
            "--press", "service@2+2",
            "--press", "test@3+2",
            "--press", "paddle=0x123@4+2"
        )
    }

    $fireDuration = [Math]::Max(1, $FrameCount - 120)
    $playerCount = [Math]::Min(4, [Math]::Max(1, $GameplayPlayers))
    $args = [System.Collections.Generic.List[string]]::new()
    for ($player = 1; $player -le $playerCount; ++$player) {
        $prefix = "p${player}:"
        $period = 300
        for ($baseFrame = 0; $baseFrame + 60 -lt $FrameCount; $baseFrame += $period) {
            $args.Add("--press")
            $args.Add("${prefix}select@$($baseFrame + 60)+6")
            if (-not $UseGameplayRepeat) {
                break
            }
        }
        for ($baseFrame = 0; $baseFrame + 90 -lt $FrameCount; $baseFrame += $period) {
            $args.Add("--press")
            $args.Add("${prefix}start@$($baseFrame + 90)+6")
            if (-not $UseGameplayRepeat) {
                break
            }
        }
        foreach ($button in @("a", "b", "c")) {
            $args.Add("--press")
            $args.Add("${prefix}${button}@120+$fireDuration")
        }
    }
    return $args.ToArray()
}

function Get-Cps2SetTokenSet {
    param([AllowEmptyCollection()][string[]]$Values)

    $tokens = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($part in ($value -split '[,;]')) {
            $trimmed = $part.Trim()
            if ($trimmed.Length -eq 0) {
                continue
            }
            [void]$tokens.Add($trimmed)
            if ($trimmed.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
                [void]$tokens.Add([System.IO.Path]::GetFileNameWithoutExtension($trimmed))
            } else {
                [void]$tokens.Add("$trimmed.zip")
            }
        }
    }
    return ,$tokens
}

function Test-Cps2SetTokenMatch {
    param(
        [Parameter(Mandatory = $true)][string]$RomPath,
        [Parameter(Mandatory = $true)][System.Collections.Generic.HashSet[string]]$Tokens
    )

    if ($Tokens.Count -eq 0) {
        return $false
    }
    $zipName = [System.IO.Path]::GetFileName($RomPath)
    $setId = [System.IO.Path]::GetFileNameWithoutExtension($RomPath)
    return ($Tokens.Contains($zipName) -or $Tokens.Contains($setId))
}

function Write-Cps2CorpusArtifacts {
    param(
        [Parameter(Mandatory = $true)][System.Collections.Generic.List[object]]$Results,
        [Parameter(Mandatory = $true)][System.Collections.Generic.List[object]]$FrameRows,
        [Parameter(Mandatory = $true)][string]$SummaryPath,
        [Parameter(Mandatory = $true)][string]$FrameHashCsvPath
    )

    $Results | ConvertTo-Json -Depth 4 | Set-Content -Path $SummaryPath -Encoding utf8
    $FrameRows | Export-Csv -Path $FrameHashCsvPath -NoTypeInformation -Encoding utf8
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
        if ($ManifestIds.Contains($stem.Substring(0, $underscore))) {
            return $true
        }
    }

    $bestPrefixLength = 0
    foreach ($id in $ManifestIds) {
        if ($id.Length -lt 3) {
            continue
        }
        if ($stem.Length -le $id.Length) {
            continue
        }
        if ($stem.StartsWith($id, [System.StringComparison]::OrdinalIgnoreCase) -and
            $id.Length -gt $bestPrefixLength) {
            $bestPrefixLength = $id.Length
        }
    }
    return $bestPrefixLength -gt 0
}

if ([string]::IsNullOrWhiteSpace($Rom)) {
    $Rom = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_ROM")
}
if ([string]::IsNullOrWhiteSpace($RomDir)) {
    $RomDir = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_SET_DIR")
}

$expectedFrameHashPath = ""
$expectedZipSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
if (-not [string]::IsNullOrWhiteSpace($ExpectedFrameHashes)) {
    $resolvedExpected = Resolve-RepoPath $ExpectedFrameHashes
    if (-not (Test-Path -LiteralPath $resolvedExpected -PathType Leaf)) {
        throw "Expected CPS2 frame-hash CSV not found: $ExpectedFrameHashes"
    }
    $expectedFrameHashPath = (Resolve-Path -LiteralPath $resolvedExpected).Path
    foreach ($expected in @(Import-Csv -LiteralPath $expectedFrameHashPath)) {
        $zip = Get-CsvValue -Row $expected -Name "zip"
        if (-not [string]::IsNullOrWhiteSpace($zip)) {
            [void]$expectedZipSet.Add($zip)
        }
    }
}

$buildRoot = Resolve-RepoPath $BuildDir
$player = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "mnemos_player.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($player)) {
    throw "mnemos_player.exe not found under '$buildRoot'. Build mnemos_player first."
}

$roms = [System.Collections.Generic.List[string]]::new()
$skippedZips = [System.Collections.Generic.List[string]]::new()
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
                if ($expectedZipSet.Count -eq 0 -or $expectedZipSet.Contains($zip.Name)) {
                    $roms.Add($zip.FullName)
                } else {
                    $skippedZips.Add($zip.FullName)
                }
            } else {
                $skippedZips.Add($zip.FullName)
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
$onlySetTokens = Get-Cps2SetTokenSet -Values $OnlySets
if ($onlySetTokens.Count -gt 0) {
    $uniqueRoms = @($uniqueRoms | Where-Object { Test-Cps2SetTokenMatch -RomPath $_ -Tokens $onlySetTokens })
}

$skipSetTokens = Get-Cps2SetTokenSet -Values $SkipSets
if ($skipSetTokens.Count -gt 0) {
    $uniqueRoms = @($uniqueRoms | Where-Object { -not (Test-Cps2SetTokenMatch -RomPath $_ -Tokens $skipSetTokens) })
}

if (-not [string]::IsNullOrWhiteSpace($StartAfter)) {
    $startAfterTokens = Get-Cps2SetTokenSet -Values @($StartAfter)
    $afterStartSet = $false
    $foundStartSet = $false
    $resumedRoms = [System.Collections.Generic.List[string]]::new()
    foreach ($romPath in $uniqueRoms) {
        if ($afterStartSet) {
            $resumedRoms.Add($romPath)
            continue
        }
        if (Test-Cps2SetTokenMatch -RomPath $romPath -Tokens $startAfterTokens) {
            $afterStartSet = $true
            $foundStartSet = $true
        }
    }
    if (-not $foundStartSet) {
        throw "StartAfter set '$StartAfter' was not found in the selected CPS2 ROM list."
    }
    $uniqueRoms = @($resumedRoms)
}

if ($MaxSets -gt 0) {
    $uniqueRoms = @($uniqueRoms | Select-Object -First $MaxSets)
}

$requireAllExpectedRows = (
    [string]::IsNullOrWhiteSpace($Rom) -and
    $onlySetTokens.Count -eq 0 -and
    $skipSetTokens.Count -eq 0 -and
    [string]::IsNullOrWhiteSpace($StartAfter) -and
    $MaxSets -le 0
)

if ($uniqueRoms.Count -eq 0) {
    if ($skippedZips.Count -gt 0 -and -not $IncludeAllZips) {
        Write-Warning ("No selected CPS2 ROMs; {0} zip(s) were skipped by the manifest/baseline filter. Use -IncludeAllZips or adjust -ExpectedFrameHashes to force them." -f $skippedZips.Count)
    }
    Write-Host "No CPS2 ROMs configured; set MNEMOS_CPS2_ROM or MNEMOS_CPS2_SET_DIR to run this gate." -ForegroundColor DarkGray
    exit 0
}

$stamp = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss-fff"), $PID
$outDir = Join-Path $repoRoot "build/scratch/cps2-corpus/$stamp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if ($skippedZips.Count -gt 0 -and -not $IncludeAllZips) {
    $skippedPath = Join-Path $outDir "skipped-zips.txt"
    $skippedZips | Set-Content -Path $skippedPath -Encoding utf8
    Write-Host ("[cps2] skipped {0} non-matching zip(s); list: {1}" -f $skippedZips.Count, $skippedPath) -ForegroundColor DarkGray
}

$results = [System.Collections.Generic.List[object]]::new()
$frameRows = [System.Collections.Generic.List[object]]::new()
$summaryPath = Join-Path $outDir "summary.json"
$frameHashCsvPath = Join-Path $outDir "frame_hashes.csv"
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
    $qsoundRegisterDumpPath = "$screenshotPath.dl_1425.regs.txt"
    $audioBase = Join-Path $setOut "$setId.audio"
    $audioPath = "$audioBase.rendered.wav"
    $audioLog = Join-Path $setOut "$setId.audio.log"
    $audioStateScreenshotPath = "$audioBase.after-audio.ppm"
    $audioQSoundRegisterDumpPath = "$audioStateScreenshotPath.dl_1425.regs.txt"
    $audioStateLog = Join-Path $setOut "$setId.audio-state.log"
    $batteryPath = Join-Path $setOut "$setId.eeprom.srm"
    $batteryLog = Join-Path $setOut "$setId.eeprom.log"

    Write-Host ("[cps2] {0}" -f $setId) -ForegroundColor Cyan

    $effectiveFrames = Get-Cps2SaveFrameCount -SetId $setId -DefaultFrames $Frames
    $defaultAudioFrames = Get-Cps2AudioFrameCount -SetId $setId -DefaultFrames $effectiveFrames
    $effectiveAudioFrames = if ($AudioFrames -gt 0) { $AudioFrames } else { $defaultAudioFrames }
    $effectiveAudioStateProbe = $AudioStateProbe.IsPresent -or (Test-Cps2AudioStateProbeDefault -SetId $setId)
    $effectiveAudioGameplayInput =
        $GameplayInput.IsPresent -or (Test-Cps2AudioGameplayProbeDefault -SetId $setId)
    $effectiveAudioGameplayRepeat = $GameplayRepeat.IsPresent -or
        (Test-Cps2AudioGameplayProbeDefault -SetId $setId)
    $effectiveAudioGameplayPlayers =
        Get-Cps2AudioGameplayPlayerCount -SetId $setId -DefaultPlayers $GameplayPlayers
    $savePressArgs = Get-Cps2PressArguments -FrameCount $effectiveFrames `
        -UseGameplayInput:$GameplaySaveInput.IsPresent `
        -GameplayPlayers $GameplayPlayers `
        -UseGameplayRepeat:$GameplayRepeat.IsPresent
    $audioPressArgs = Get-Cps2PressArguments -FrameCount $effectiveAudioFrames `
        -UseGameplayInput:$effectiveAudioGameplayInput `
        -GameplayPlayers $effectiveAudioGameplayPlayers `
        -UseGameplayRepeat:$effectiveAudioGameplayRepeat

    $saveArgs = @(
        "--system", "cps2",
        "--rom", $romPath,
        "--frames", $effectiveFrames.ToString()
    ) + $savePressArgs + @("--save-state", $statePath)
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

    $batteryExit = $null
    if ($saveExit -eq 0) {
        $batteryArgs = @(
            "--system", "cps2",
            "--rom", $romPath,
            "--load-state", $statePath,
            "--dump-battery", $batteryPath
        )
        $batteryExit = Invoke-Player -Player $player -LogPath $batteryLog -Arguments $batteryArgs
    }

    $audioArgs = @(
        "--system", "cps2",
        "--rom", $romPath,
        "--extract-audio", $audioBase,
        "--extract-frames", $effectiveAudioFrames.ToString()
    ) + $audioPressArgs
    $audioExit = Invoke-Player -Player $player -LogPath $audioLog -Arguments $audioArgs

    $audioStateExit = $null
    if ($effectiveAudioStateProbe) {
        $audioStateArgs = @(
            "--system", "cps2",
            "--rom", $romPath,
            "--frames", $effectiveAudioFrames.ToString(),
            "--screenshot", $audioStateScreenshotPath
        ) + $audioPressArgs
        $audioStateExit = Invoke-Player -Player $player -LogPath $audioStateLog -Arguments $audioStateArgs
    }

    $frameProbe = $null
    $frameError = ""
    if ($saveExit -eq 0 -and $loadExit -eq 0 -and (Test-Path -LiteralPath $screenshotPath)) {
        try {
            $frameProbe = Get-PpmFrameProbe -Path $screenshotPath
        } catch {
            $frameError = $_.Exception.Message
        }
    }

    $qsoundProbe = $null
    $qsoundError = ""
    if ($saveExit -eq 0 -and $loadExit -eq 0) {
        if (Test-Path -LiteralPath $qsoundRegisterDumpPath) {
            try {
                $qsoundProbe = Get-QSoundRegisterProbe -Path $qsoundRegisterDumpPath
            } catch {
                $qsoundError = $_.Exception.Message
            }
        } else {
            $qsoundError = "DL-1425 register dump missing."
        }
    }

    $audioQSoundProbe = $null
    $audioQSoundError = ""
    if ($effectiveAudioStateProbe) {
        if ($audioStateExit -eq 0 -and (Test-Path -LiteralPath $audioQSoundRegisterDumpPath)) {
            try {
                $audioQSoundProbe = Get-QSoundRegisterProbe -Path $audioQSoundRegisterDumpPath
            } catch {
                $audioQSoundError = $_.Exception.Message
            }
        } else {
            $audioQSoundError = "Audio-state DL-1425 register dump missing."
        }
    }

    $audioProbe = $null
    $audioError = ""
    if ($audioExit -eq 0 -and (Test-Path -LiteralPath $audioPath)) {
        try {
            $audioProbe = Get-WavAudioProbe -Path $audioPath -SignificantThreshold $AudioSignificantThreshold
        } catch {
            $audioError = $_.Exception.Message
        }
    }

    $batteryProbe = $null
    $batteryError = ""
    if ($batteryExit -eq 0 -and (Test-Path -LiteralPath $batteryPath)) {
        try {
            $batteryProbe = Get-BinaryProbe -Path $batteryPath -Role "cps2_93c46"
        } catch {
            $batteryError = $_.Exception.Message
        }
    }

    $qsoundProgrammedSilent = if ($null -ne $qsoundProbe -and $null -ne $audioProbe -and
        $qsoundProbe.qsound_programmed_audio_commands -gt 0 -and
        $audioProbe.audio_nonzero_samples -eq 0) {
        1
    } else {
        0
    }

    $passed = ($saveExit -eq 0 -and $loadExit -eq 0 -and (Test-Path -LiteralPath $statePath) -and
        (Test-Path -LiteralPath $screenshotPath) -and $null -ne $frameProbe -and
        $frameProbe.frame_nonzero_rgb_pixels -gt 0 -and $audioExit -eq 0 -and
        (Test-Path -LiteralPath $audioPath) -and $null -ne $audioProbe -and
        $audioProbe.audio_frame_count -gt 0 -and $batteryExit -eq 0 -and
        (Test-Path -LiteralPath $batteryPath) -and $null -ne $batteryProbe -and
        $batteryProbe.battery_bytes -gt 0 -and $null -ne $qsoundProbe -and
        (-not $effectiveAudioStateProbe -or ($audioStateExit -eq 0 -and $null -ne $audioQSoundProbe)))
    $zipName = [System.IO.Path]::GetFileName($romPath)
    $results.Add([pscustomobject]@{
        set = $setId
        zip = $zipName
        rom = $romPath
        save_exit = $saveExit
        load_exit = $loadExit
        audio_exit = $audioExit
        battery_exit = $batteryExit
        passed = $passed
        audio_frames = $effectiveAudioFrames
        frame_width = if ($null -ne $frameProbe) { $frameProbe.frame_width } else { 0 }
        frame_height = if ($null -ne $frameProbe) { $frameProbe.frame_height } else { 0 }
        frame_rgb_hash_algorithm = if ($null -ne $frameProbe) { $frameProbe.frame_rgb_hash_algorithm } else { "" }
        frame_rgb_hash = if ($null -ne $frameProbe) { $frameProbe.frame_rgb_hash } else { "" }
        frame_nonzero_rgb_pixels = if ($null -ne $frameProbe) { $frameProbe.frame_nonzero_rgb_pixels } else { 0 }
        frame_error = $frameError
        audio_sample_rate = if ($null -ne $audioProbe) { $audioProbe.audio_sample_rate } else { 0 }
        audio_channels = if ($null -ne $audioProbe) { $audioProbe.audio_channels } else { 0 }
        audio_bits_per_sample = if ($null -ne $audioProbe) { $audioProbe.audio_bits_per_sample } else { 0 }
        audio_frame_count = if ($null -ne $audioProbe) { $audioProbe.audio_frame_count } else { 0 }
        audio_pcm_hash_algorithm = if ($null -ne $audioProbe) { $audioProbe.audio_pcm_hash_algorithm } else { "" }
        audio_pcm_hash = if ($null -ne $audioProbe) { $audioProbe.audio_pcm_hash } else { "" }
        audio_nonzero_samples = if ($null -ne $audioProbe) { $audioProbe.audio_nonzero_samples } else { 0 }
        audio_peak_abs = if ($null -ne $audioProbe) { $audioProbe.audio_peak_abs } else { 0 }
        audio_first_nonzero_sample = if ($null -ne $audioProbe) { $audioProbe.audio_first_nonzero_sample } else { -1 }
        audio_first_nonzero_frame = if ($null -ne $audioProbe) { $audioProbe.audio_first_nonzero_frame } else { -1 }
        audio_last_nonzero_sample = if ($null -ne $audioProbe) { $audioProbe.audio_last_nonzero_sample } else { -1 }
        audio_last_nonzero_frame = if ($null -ne $audioProbe) { $audioProbe.audio_last_nonzero_frame } else { -1 }
        audio_significant_threshold = if ($null -ne $audioProbe) { $audioProbe.audio_significant_threshold } else { $AudioSignificantThreshold }
        audio_significant_samples = if ($null -ne $audioProbe) { $audioProbe.audio_significant_samples } else { 0 }
        audio_first_significant_sample = if ($null -ne $audioProbe) { $audioProbe.audio_first_significant_sample } else { -1 }
        audio_first_significant_frame = if ($null -ne $audioProbe) { $audioProbe.audio_first_significant_frame } else { -1 }
        audio_last_significant_sample = if ($null -ne $audioProbe) { $audioProbe.audio_last_significant_sample } else { -1 }
        audio_last_significant_frame = if ($null -ne $audioProbe) { $audioProbe.audio_last_significant_frame } else { -1 }
        audio_error = $audioError
        qsound_probe_role = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_probe_role } else { "" }
        qsound_probe_present = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_probe_present } else { 0 }
        qsound_port_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_port_writes } else { 0 }
        qsound_register_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_register_writes } else { 0 }
        qsound_last_reg = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_last_reg } else { "" }
        qsound_last_data = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_last_data } else { "" }
        qsound_last_pc = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_last_pc } else { "" }
        qsound_pcm_volume_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_pcm_volume_writes } else { 0 }
        qsound_adpcm_volume_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_volume_writes } else { 0 }
        qsound_adpcm_triggers = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_triggers } else { 0 }
        qsound_adpcm_configured_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_configured_voices } else { 0 }
        qsound_adpcm_flagged_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_flagged_voices } else { 0 }
        qsound_adpcm_nonzero_volume_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_nonzero_volume_voices } else { 0 }
        qsound_adpcm_nonzero_range_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_nonzero_range_voices } else { 0 }
        qsound_adpcm_trigger_only_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_trigger_only_voices } else { 0 }
        qsound_programmed_audio_commands = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_programmed_audio_commands } else { 0 }
        qsound_programmed_silent = $qsoundProgrammedSilent
        qsound_error = $qsoundError
        audio_qsound_probe_present = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_probe_present } else { 0 }
        audio_qsound_port_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_port_writes } else { 0 }
        audio_qsound_register_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_register_writes } else { 0 }
        audio_qsound_last_reg = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_last_reg } else { "" }
        audio_qsound_last_data = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_last_data } else { "" }
        audio_qsound_last_pc = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_last_pc } else { "" }
        audio_qsound_pcm_volume_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_pcm_volume_writes } else { 0 }
        audio_qsound_adpcm_volume_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_volume_writes } else { 0 }
        audio_qsound_adpcm_triggers = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_triggers } else { 0 }
        audio_qsound_adpcm_configured_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_configured_voices } else { 0 }
        audio_qsound_adpcm_flagged_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_flagged_voices } else { 0 }
        audio_qsound_adpcm_nonzero_volume_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_nonzero_volume_voices } else { 0 }
        audio_qsound_adpcm_nonzero_range_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_nonzero_range_voices } else { 0 }
        audio_qsound_adpcm_trigger_only_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_trigger_only_voices } else { 0 }
        audio_qsound_programmed_audio_commands = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_programmed_audio_commands } else { 0 }
        audio_qsound_error = $audioQSoundError
        battery_role = if ($null -ne $batteryProbe) { $batteryProbe.battery_role } else { "" }
        battery_bytes = if ($null -ne $batteryProbe) { $batteryProbe.battery_bytes } else { 0 }
        battery_hash_algorithm = if ($null -ne $batteryProbe) { $batteryProbe.battery_hash_algorithm } else { "" }
        battery_hash = if ($null -ne $batteryProbe) { $batteryProbe.battery_hash } else { "" }
        battery_non_ff_bytes = if ($null -ne $batteryProbe) { $batteryProbe.battery_non_ff_bytes } else { 0 }
        battery_error = $batteryError
        state = $statePath
        screenshot = $screenshotPath
        qsound_register_dump = $qsoundRegisterDumpPath
        audio = $audioPath
        audio_qsound_register_dump = $audioQSoundRegisterDumpPath
        audio_state_log = $audioStateLog
        battery = $batteryPath
        save_log = $saveLog
        load_log = $loadLog
        audio_log = $audioLog
        battery_log = $batteryLog
    })
    $frameRows.Add([pscustomobject]@{
        zip = $zipName
        game = $setId
        frames = $effectiveFrames
        audio_frames = $effectiveAudioFrames
        frame_width = if ($null -ne $frameProbe) { $frameProbe.frame_width } else { 0 }
        frame_height = if ($null -ne $frameProbe) { $frameProbe.frame_height } else { 0 }
        frame_rgb_format = if ($null -ne $frameProbe) { $frameProbe.frame_rgb_format } else { "" }
        frame_rgb_hash_algorithm = if ($null -ne $frameProbe) { $frameProbe.frame_rgb_hash_algorithm } else { "" }
        frame_rgb_hash = if ($null -ne $frameProbe) { $frameProbe.frame_rgb_hash } else { "" }
        frame_nonzero_rgb_pixels = if ($null -ne $frameProbe) { $frameProbe.frame_nonzero_rgb_pixels } else { 0 }
        audio_sample_rate = if ($null -ne $audioProbe) { $audioProbe.audio_sample_rate } else { 0 }
        audio_channels = if ($null -ne $audioProbe) { $audioProbe.audio_channels } else { 0 }
        audio_bits_per_sample = if ($null -ne $audioProbe) { $audioProbe.audio_bits_per_sample } else { 0 }
        audio_frame_count = if ($null -ne $audioProbe) { $audioProbe.audio_frame_count } else { 0 }
        audio_pcm_hash_algorithm = if ($null -ne $audioProbe) { $audioProbe.audio_pcm_hash_algorithm } else { "" }
        audio_pcm_hash = if ($null -ne $audioProbe) { $audioProbe.audio_pcm_hash } else { "" }
        audio_nonzero_samples = if ($null -ne $audioProbe) { $audioProbe.audio_nonzero_samples } else { 0 }
        audio_peak_abs = if ($null -ne $audioProbe) { $audioProbe.audio_peak_abs } else { 0 }
        audio_first_nonzero_sample = if ($null -ne $audioProbe) { $audioProbe.audio_first_nonzero_sample } else { -1 }
        audio_first_nonzero_frame = if ($null -ne $audioProbe) { $audioProbe.audio_first_nonzero_frame } else { -1 }
        audio_last_nonzero_sample = if ($null -ne $audioProbe) { $audioProbe.audio_last_nonzero_sample } else { -1 }
        audio_last_nonzero_frame = if ($null -ne $audioProbe) { $audioProbe.audio_last_nonzero_frame } else { -1 }
        audio_significant_threshold = if ($null -ne $audioProbe) { $audioProbe.audio_significant_threshold } else { $AudioSignificantThreshold }
        audio_significant_samples = if ($null -ne $audioProbe) { $audioProbe.audio_significant_samples } else { 0 }
        audio_first_significant_sample = if ($null -ne $audioProbe) { $audioProbe.audio_first_significant_sample } else { -1 }
        audio_first_significant_frame = if ($null -ne $audioProbe) { $audioProbe.audio_first_significant_frame } else { -1 }
        audio_last_significant_sample = if ($null -ne $audioProbe) { $audioProbe.audio_last_significant_sample } else { -1 }
        audio_last_significant_frame = if ($null -ne $audioProbe) { $audioProbe.audio_last_significant_frame } else { -1 }
        qsound_probe_role = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_probe_role } else { "" }
        qsound_probe_present = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_probe_present } else { 0 }
        qsound_port_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_port_writes } else { 0 }
        qsound_register_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_register_writes } else { 0 }
        qsound_last_reg = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_last_reg } else { "" }
        qsound_last_data = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_last_data } else { "" }
        qsound_last_pc = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_last_pc } else { "" }
        qsound_pcm_volume_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_pcm_volume_writes } else { 0 }
        qsound_adpcm_volume_writes = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_volume_writes } else { 0 }
        qsound_adpcm_triggers = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_triggers } else { 0 }
        qsound_adpcm_configured_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_configured_voices } else { 0 }
        qsound_adpcm_flagged_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_flagged_voices } else { 0 }
        qsound_adpcm_nonzero_volume_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_nonzero_volume_voices } else { 0 }
        qsound_adpcm_nonzero_range_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_nonzero_range_voices } else { 0 }
        qsound_adpcm_trigger_only_voices = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_adpcm_trigger_only_voices } else { 0 }
        qsound_programmed_audio_commands = if ($null -ne $qsoundProbe) { $qsoundProbe.qsound_programmed_audio_commands } else { 0 }
        qsound_programmed_silent = $qsoundProgrammedSilent
        audio_qsound_probe_present = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_probe_present } else { 0 }
        audio_qsound_port_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_port_writes } else { 0 }
        audio_qsound_register_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_register_writes } else { 0 }
        audio_qsound_last_reg = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_last_reg } else { "" }
        audio_qsound_last_data = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_last_data } else { "" }
        audio_qsound_last_pc = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_last_pc } else { "" }
        audio_qsound_pcm_volume_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_pcm_volume_writes } else { 0 }
        audio_qsound_adpcm_volume_writes = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_volume_writes } else { 0 }
        audio_qsound_adpcm_triggers = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_triggers } else { 0 }
        audio_qsound_adpcm_configured_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_configured_voices } else { 0 }
        audio_qsound_adpcm_flagged_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_flagged_voices } else { 0 }
        audio_qsound_adpcm_nonzero_volume_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_nonzero_volume_voices } else { 0 }
        audio_qsound_adpcm_nonzero_range_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_nonzero_range_voices } else { 0 }
        audio_qsound_adpcm_trigger_only_voices = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_adpcm_trigger_only_voices } else { 0 }
        audio_qsound_programmed_audio_commands = if ($null -ne $audioQSoundProbe) { $audioQSoundProbe.qsound_programmed_audio_commands } else { 0 }
        battery_role = if ($null -ne $batteryProbe) { $batteryProbe.battery_role } else { "" }
        battery_bytes = if ($null -ne $batteryProbe) { $batteryProbe.battery_bytes } else { 0 }
        battery_hash_algorithm = if ($null -ne $batteryProbe) { $batteryProbe.battery_hash_algorithm } else { "" }
        battery_hash = if ($null -ne $batteryProbe) { $batteryProbe.battery_hash } else { "" }
        battery_non_ff_bytes = if ($null -ne $batteryProbe) { $batteryProbe.battery_non_ff_bytes } else { 0 }
    })
    Write-Cps2CorpusArtifacts -Results $results -FrameRows $frameRows -SummaryPath $summaryPath -FrameHashCsvPath $frameHashCsvPath
}

Write-Cps2CorpusArtifacts -Results $results -FrameRows $frameRows -SummaryPath $summaryPath -FrameHashCsvPath $frameHashCsvPath

$hashMismatches = @()
if (-not [string]::IsNullOrWhiteSpace($expectedFrameHashPath)) {
    $hashMismatches = @(Compare-FrameHashRows -CurrentRows @($frameRows) `
            -ExpectedCsvPath $expectedFrameHashPath `
            -RequireAllExpectedRows $requireAllExpectedRows)
    if ($hashMismatches.Count -gt 0) {
        $frameHashMismatchPath = Join-Path $outDir "frame_hash_mismatches.csv"
        $hashMismatches | Export-Csv -Path $frameHashMismatchPath -NoTypeInformation -Encoding utf8
        Write-Host ("[cps2] frame-hash mismatches: {0}; details: {1}" -f $hashMismatches.Count, $frameHashMismatchPath) -ForegroundColor Red
    }
}

$failed = @($results | Where-Object { -not $_.passed })
Write-Host ("CPS2 corpus smoke: {0}/{1} passed; summary: {2}; hashes: {3}" -f ($results.Count - $failed.Count), $results.Count, $summaryPath, $frameHashCsvPath)
if ($failed.Count -gt 0 -or $hashMismatches.Count -gt 0) {
    foreach ($row in $failed) {
        Write-Host ("  [fail] {0} save={1} load={2} audio={3} battery={4} frame_nonzero={5} audio_frames={6} battery_bytes={7} frame_error={8} audio_error={9} qsound_error={10} battery_error={11}" -f $row.set, $row.save_exit, $row.load_exit, $row.audio_exit, $row.battery_exit, $row.frame_nonzero_rgb_pixels, $row.audio_frame_count, $row.battery_bytes, $row.frame_error, $row.audio_error, $row.qsound_error, $row.battery_error) -ForegroundColor Red
    }
    exit 1
}

exit 0
