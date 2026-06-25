#!/usr/bin/env pwsh
# Focused HSF2 CPS2/QSound oracle check against the older Emu probe.

param(
    [string]$BuildDir = "build/windows-msvc-release",
    [string]$MnemosPlayer = "",
    [string]$OldEmuProbe = "",
    [string]$Rom = "",
    [int]$Frames = 6040,
    [int]$AudioSignificantThreshold = 64,
    [int]$AudioFirstFrameTolerance = 2,
    [int]$AudioLastFrameTolerance = 3,
    [int]$AudioPeakTolerance = 16,
    [string]$OutDir = "build/scratch/cps2-hsf2-audio-oracle"
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

function Resolve-ExistingPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $resolved = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    return (Resolve-Path -LiteralPath $resolved).Path
}

function Resolve-OptionalDefault {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        $resolved = Resolve-RepoPath $candidate
        if (Test-Path -LiteralPath $resolved -PathType Leaf) {
            return (Resolve-Path -LiteralPath $resolved).Path
        }
    }
    return ""
}

function ConvertTo-WindowsCommandArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Argument)

    if ($Argument.Length -eq 0) {
        return '""'
    }
    if ($Argument -notmatch '[\s"]') {
        return $Argument
    }

    $result = '"'
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashes
            continue
        }
        if ($character -eq '"') {
            $result += ('\' * (($backslashes * 2) + 1))
            $result += '"'
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) {
            $result += ('\' * $backslashes)
            $backslashes = 0
        }
        $result += $character
    }
    if ($backslashes -gt 0) {
        $result += ('\' * ($backslashes * 2))
    }
    $result += '"'
    return $result
}

function Join-ProcessArguments {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    return (($Arguments | ForEach-Object { ConvertTo-WindowsCommandArgument $_ }) -join ' ')
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Exe
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Arguments = Join-ProcessArguments $Arguments

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    Set-Content -LiteralPath $LogPath -Value ($stdout + $stderr) -NoNewline
    if ($process.ExitCode -ne 0) {
        throw "$Label failed with exit code $($process.ExitCode); see $LogPath"
    }
}

function Read-ProbeLog {
    param([Parameter(Mandatory = $true)][string]$Path)

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^qsound_register_([0-9A-F]{2})_writes:\s*(\d+)\s+last=([0-9A-F]{4})\s+pc=([0-9A-F]{4})') {
            $reg = $matches[1]
            $values["qsound_register_${reg}_writes"] = $matches[2]
            $values["qsound_register_${reg}_last"] = $matches[3]
            $values["qsound_register_${reg}_pc"] = $matches[4]
            continue
        }
        if ($line -match '^([A-Za-z0-9_]+):\s*(.+?)\s*$') {
            $values[$matches[1]] = $matches[2]
            continue
        }
    }
    return $values
}

function Read-RegisterDump {
    param([Parameter(Mandatory = $true)][string]$Path)

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^(\S+)\s+bits=\d+\s+format=\S+\s+value=0x([0-9A-Fa-f]+)') {
            $values[$matches[1]] = [Convert]::ToUInt64($matches[2], 16)
        }
    }
    return $values
}

function Convert-Decimal {
    param(
        [Parameter(Mandatory = $true)]$Values,
        [Parameter(Mandatory = $true)][string]$Key
    )
    if (-not $Values.ContainsKey($Key)) {
        throw "missing decimal key '$Key'"
    }
    return [Convert]::ToUInt64(([string]$Values[$Key]).Trim(), 10)
}

function Convert-Hex {
    param(
        [Parameter(Mandatory = $true)]$Values,
        [Parameter(Mandatory = $true)][string]$Key
    )
    if (-not $Values.ContainsKey($Key)) {
        throw "missing hex key '$Key'"
    }
    return [Convert]::ToUInt64(([string]$Values[$Key]).Trim(), 16)
}

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Failures,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][UInt64]$Expected,
        [Parameter(Mandatory = $true)][UInt64]$Actual
    )
    if ($Expected -ne $Actual) {
        $Failures.Add(("{0}: expected {1}, got {2}" -f $Name, $Expected, $Actual))
    }
}

function Assert-Near {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Failures,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$Expected,
        [Parameter(Mandatory = $true)][int]$Actual,
        [Parameter(Mandatory = $true)][int]$Tolerance
    )
    if ([Math]::Abs($Expected - $Actual) -gt $Tolerance) {
        $Failures.Add(("{0}: expected {1} +/- {2}, got {3}" -f $Name, $Expected, $Tolerance, $Actual))
    }
}

function Read-WavStats {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$Frames,
        [Parameter(Mandatory = $true)][int]$SignificantThreshold
    )

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        $riff = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
        $null = $reader.ReadUInt32()
        $wave = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
            throw "not a RIFF/WAVE file: $Path"
        }

        [UInt16]$channels = 0
        [UInt32]$sampleRate = 0
        [UInt16]$bitsPerSample = 0
        [Int64]$dataOffset = -1
        [UInt32]$dataSize = 0

        while ($stream.Position -lt $stream.Length) {
            $chunkId = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
            $chunkSize = $reader.ReadUInt32()
            $chunkData = $stream.Position
            if ($chunkId -eq "fmt ") {
                $formatTag = $reader.ReadUInt16()
                $channels = $reader.ReadUInt16()
                $sampleRate = $reader.ReadUInt32()
                $null = $reader.ReadUInt32()
                $null = $reader.ReadUInt16()
                $bitsPerSample = $reader.ReadUInt16()
                if ($formatTag -ne 1) {
                    throw "unsupported WAV format tag $formatTag in $Path"
                }
            } elseif ($chunkId -eq "data") {
                $dataOffset = $stream.Position
                $dataSize = $chunkSize
            }
            $stream.Position = $chunkData + $chunkSize
            if (($chunkSize % 2) -ne 0) {
                $stream.Position += 1
            }
        }

        if ($channels -ne 2 -or $bitsPerSample -ne 16 -or $dataOffset -lt 0) {
            throw "expected stereo 16-bit PCM data in $Path"
        }

        $sampleFrames = [int]($dataSize / ($channels * 2))
        $stream.Position = $dataOffset
        $data = $reader.ReadBytes([int]$dataSize)

        [int]$firstNonzero = -1
        [int]$lastNonzero = -1
        [int]$firstSignificant = -1
        [int]$lastSignificant = -1
        [int]$peak = 0
        [int]$nonzeroSamples = 0
        [int]$nonzeroSampleFrames = 0

        for ($frame = 0; $frame -lt $sampleFrames; ++$frame) {
            $base = $frame * 4
            $left = [System.BitConverter]::ToInt16($data, $base)
            $right = [System.BitConverter]::ToInt16($data, $base + 2)
            $absLeft = [Math]::Abs([int]$left)
            $absRight = [Math]::Abs([int]$right)
            if ($left -ne 0) {
                ++$nonzeroSamples
            }
            if ($right -ne 0) {
                ++$nonzeroSamples
            }
            $framePeak = [Math]::Max($absLeft, $absRight)
            if ($framePeak -gt $peak) {
                $peak = $framePeak
            }
            if ($framePeak -ne 0) {
                ++$nonzeroSampleFrames
                if ($firstNonzero -lt 0) {
                    $firstNonzero = $frame
                }
                $lastNonzero = $frame
            }
            if ($framePeak -ge $SignificantThreshold) {
                if ($firstSignificant -lt 0) {
                    $firstSignificant = $frame
                }
                $lastSignificant = $frame
            }
        }

        $toEmulatedFrame = {
            param([int]$SampleFrame)
            if ($SampleFrame -lt 0 -or $sampleFrames -le 0) {
                return -1
            }
            return [int][Math]::Floor(([double]$SampleFrame * [double]$Frames) / [double]$sampleFrames)
        }

        return [pscustomobject]@{
            Channels = [int]$channels
            SampleRate = [int]$sampleRate
            BitsPerSample = [int]$bitsPerSample
            AudioFrames = $sampleFrames
            NonzeroSamples = $nonzeroSamples
            NonzeroSampleFrames = $nonzeroSampleFrames
            Peak = $peak
            FirstNonzeroSampleFrame = $firstNonzero
            LastNonzeroSampleFrame = $lastNonzero
            FirstSignificantSampleFrame = $firstSignificant
            LastSignificantSampleFrame = $lastSignificant
            FirstNonzeroEmulatedFrame = & $toEmulatedFrame $firstNonzero
            LastNonzeroEmulatedFrame = & $toEmulatedFrame $lastNonzero
            FirstSignificantEmulatedFrame = & $toEmulatedFrame $firstSignificant
            LastSignificantEmulatedFrame = & $toEmulatedFrame $lastSignificant
        }
    } finally {
        $stream.Dispose()
    }
}

$mnemosPlayerDefault = Join-Path (Resolve-RepoPath $BuildDir) "src/apps/player/mnemos_player.exe"
$romDefault = Resolve-OptionalDefault @(
    $Rom,
    $env:MNEMOS_CPS2_HSF2_ROM,
    $env:MNEMOS_CPS2_ROM,
    "D:\emu\capcom\cps2\hsf2.zip"
)
$oldProbeDefault = Resolve-OptionalDefault @(
    $OldEmuProbe,
    $env:MNEMOS_OLD_EMU_CPS2_PROBE,
    "C:\Users\mkrol\source\repos\Emu\out\build\x64-debug\tools\cps2_probe.exe"
)

if ([string]::IsNullOrWhiteSpace($MnemosPlayer)) {
    $MnemosPlayer = $mnemosPlayerDefault
}
if ([string]::IsNullOrWhiteSpace($romDefault)) {
    throw "HSF2 ROM not found; pass -Rom or set MNEMOS_CPS2_HSF2_ROM"
}
if ([string]::IsNullOrWhiteSpace($oldProbeDefault)) {
    throw "old Emu cps2_probe.exe not found; pass -OldEmuProbe or set MNEMOS_OLD_EMU_CPS2_PROBE"
}

$mnemosPlayerPath = Resolve-ExistingPath $MnemosPlayer "mnemos_player"
$oldProbePath = Resolve-ExistingPath $oldProbeDefault "old Emu CPS2 probe"
$romPath = Resolve-ExistingPath $romDefault "HSF2 ROM"
$outPath = Resolve-RepoPath $OutDir
New-Item -ItemType Directory -Force -Path $outPath | Out-Null

if ($Frames -le 0) {
    throw "-Frames must be positive"
}

# The old Emu CPS2 probe runs the 68K at 11.8 MHz and advances one NTSC CPS2
# frame with the ceiling of that clock divided by 59.637405 Hz.
[UInt64]$cyclesPerFrame = 197863
[UInt64]$cycles = $cyclesPerFrame * [UInt64]$Frames

$oldLog = Join-Path $outPath ("oldemu_hsf2_{0}.log" -f $Frames)
$oldAudioWav = Join-Path $outPath ("oldemu_hsf2_audio_{0}.wav" -f $Frames)
$mnemosLog = Join-Path $outPath ("mnemos_hsf2_{0}.log" -f $Frames)
$mnemosPpm = Join-Path $outPath ("mnemos_hsf2_{0}.ppm" -f $Frames)
$audioBase = Join-Path $outPath ("mnemos_hsf2_audio_{0}" -f $Frames)
$audioLog = Join-Path $outPath ("mnemos_hsf2_audio_{0}.log" -f $Frames)

Invoke-Checked -Exe $oldProbePath `
    -Arguments @("--run-cycles", $cycles.ToString(), "--audio-out", $oldAudioWav, $romPath) `
    -LogPath $oldLog `
    -Label "old Emu HSF2 probe"

Invoke-Checked -Exe $mnemosPlayerPath `
    -Arguments @("--system", "cps2", "--rom", $romPath, "--run-cycles", $cycles.ToString(), "--screenshot", $mnemosPpm) `
    -LogPath $mnemosLog `
    -Label "Mnemos HSF2 cycle probe"

Invoke-Checked -Exe $mnemosPlayerPath `
    -Arguments @("--system", "cps2", "--rom", $romPath, "--extract-audio", $audioBase, "--extract-frames", $Frames.ToString()) `
    -LogPath $audioLog `
    -Label "Mnemos HSF2 audio export"

$old = Read-ProbeLog $oldLog
$dl1425 = Read-RegisterDump "$mnemosPpm.dl_1425.regs.txt"
$z80 = Read-RegisterDump "$mnemosPpm.z80.regs.txt"
$bus = Read-RegisterDump "$mnemosPpm.cps2_bus.regs.txt"
$oldWav = Read-WavStats $oldAudioWav $Frames $AudioSignificantThreshold
$wav = Read-WavStats "$audioBase.rendered.wav" $Frames $AudioSignificantThreshold
$failures = [System.Collections.Generic.List[string]]::new()

Assert-Equal $failures "run cycles executed" (Convert-Decimal $old "run_cycles_executed") $bus["MAINCYC"]
Assert-Equal $failures "Z80 PC" (Convert-Hex $old "sound_pc") $z80["PC"]
Assert-Equal $failures "Z80 SP" (Convert-Hex $old "sound_sp") $z80["SP"]
Assert-Equal $failures "Z80 AF" (Convert-Hex $old "sound_af") $z80["AF"]
Assert-Equal $failures "Z80 BC" (Convert-Hex $old "sound_bc") $z80["BC"]
Assert-Equal $failures "Z80 DE" (Convert-Hex $old "sound_de") $z80["DE"]
Assert-Equal $failures "Z80 HL" (Convert-Hex $old "sound_hl") $z80["HL"]

Assert-Equal $failures "QSound port writes" (Convert-Decimal $old "qsound_port_writes") $dl1425["PORTWR"]
Assert-Equal $failures "QSound register writes" (Convert-Decimal $old "qsound_register_writes") $dl1425["REGWR"]
Assert-Equal $failures "QSound trace count" (Convert-Decimal $old "qsound_register_writes") $dl1425["TRACECOUNT"]
Assert-Equal $failures "QSound last register" (Convert-Hex $old "qsound_last_register") $dl1425["LASTREG"]
Assert-Equal $failures "QSound last data" (Convert-Hex $old "qsound_last_register_data") $dl1425["LASTDATA"]
Assert-Equal $failures "QSound nonzero PCM volume writes" (Convert-Decimal $old "qsound_nonzero_volume_writes") $dl1425["PCM_VOLWR"]
Assert-Equal $failures "QSound ADPCM volume writes" (Convert-Decimal $old "qsound_adpcm_nonzero_volume_writes") $dl1425["ADPCM_VOLWR"]
Assert-Equal $failures "QSound ADPCM triggers" (Convert-Decimal $old "qsound_adpcm_triggers") $dl1425["ADPCM_TRIG"]
Assert-Equal $failures "QSound register 42 writes" (Convert-Decimal $old "qsound_register_42_writes") $dl1425["REG42_WR"]
Assert-Equal $failures "QSound register 42 data" (Convert-Hex $old "qsound_register_42_last") $dl1425["REG42_DATA"]
if ($old.ContainsKey("qsound_register_D8_writes") -and $dl1425.ContainsKey("REGD8_WR")) {
    Assert-Equal $failures "QSound register D8 writes" (Convert-Decimal $old "qsound_register_D8_writes") $dl1425["REGD8_WR"]
    Assert-Equal $failures "QSound register D8 data" (Convert-Hex $old "qsound_register_D8_last") $dl1425["REGD8_DATA"]
}

Assert-Equal $failures "QSound shared 68K writes" (Convert-Decimal $old "qsound_shared_68k_writes") $bus["S68K_W"]
Assert-Equal $failures "QSound shared 68K non-FF writes" (Convert-Decimal $old "qsound_shared_68k_non_ff_writes") $bus["S68K_NFFW"]
Assert-Equal $failures "QSound shared 68K reads" (Convert-Decimal $old "qsound_shared_68k_reads") $bus["S68K_R"]
Assert-Equal $failures "QSound status reads" (Convert-Decimal $old "qsound_shared_68k_status_reads") $bus["S68K_STATUSR"]
Assert-Equal $failures "QSound magic reads" (Convert-Decimal $old "qsound_shared_68k_magic_reads") $bus["S68K_MAGICR"]
Assert-Equal $failures "QSound command writes" (Convert-Decimal $old "qsound_shared_68k_command_signal_writes") $bus["CMD68K_W"]
Assert-Equal $failures "QSound command reads" (Convert-Decimal $old "qsound_shared_z80_command_signal_reads") $bus["CMDZ80_R"]
Assert-Equal $failures "QSound shared Z80 writes" (Convert-Decimal $old "qsound_shared_z80_writes") $bus["SZ80_W"]
Assert-Equal $failures "QSound work Z80 writes" (Convert-Decimal $old "qsound_work_z80_writes") $bus["WZ80_W"]

Assert-Equal $failures "old audio-out sample frames" (Convert-Decimal $old "audio_out_sample_frames") $oldWav.AudioFrames
Assert-Equal $failures "old audio history sample frames" (Convert-Decimal $old "qsound_audio_history_samples") $oldWav.AudioFrames
Assert-Equal $failures "old audio-out sample rate" (Convert-Decimal $old "audio_out_sample_rate") $oldWav.SampleRate
Assert-Equal $failures "old WAV peak" (Convert-Decimal $old "qsound_audio_history_peak") $oldWav.Peak
Assert-Equal $failures "old WAV nonzero sample frames" (Convert-Decimal $old "qsound_audio_history_nonzero") $oldWav.NonzeroSampleFrames
Assert-Near $failures "old WAV first nonzero frame" `
    ([int](Convert-Decimal $old "qsound_audio_history_first_nonzero_frame")) `
    $oldWav.FirstNonzeroEmulatedFrame `
    1
Assert-Near $failures "old WAV last nonzero frame" `
    ([int](Convert-Decimal $old "qsound_audio_history_last_nonzero_frame")) `
    $oldWav.LastNonzeroEmulatedFrame `
    1
Assert-Near $failures "first significant audio frame" `
    $oldWav.FirstSignificantEmulatedFrame `
    $wav.FirstSignificantEmulatedFrame `
    $AudioFirstFrameTolerance
Assert-Near $failures "last significant audio frame" `
    $oldWav.LastSignificantEmulatedFrame `
    $wav.LastSignificantEmulatedFrame `
    $AudioLastFrameTolerance
Assert-Near $failures "rendered audio peak" `
    $oldWav.Peak `
    $wav.Peak `
    $AudioPeakTolerance
Assert-Equal $failures "old probe post-run audio nonzero" 0 (Convert-Decimal $old "qsound_audio_probe_nonzero")
if ($oldWav.Peak -le 0 -or $oldWav.FirstSignificantEmulatedFrame -lt 0) {
    $failures.Add("old Emu oracle WAV has no significant audio")
}
if ($wav.Peak -le 0 -or $wav.FirstSignificantEmulatedFrame -lt 0) {
    $failures.Add("Mnemos rendered WAV has no significant audio")
}

if ($failures.Count -gt 0) {
    Write-Host "HSF2 CPS2 audio oracle: FAIL"
    foreach ($failure in $failures) {
        Write-Host "  - $failure"
    }
    Write-Host "old log: $oldLog"
    Write-Host "old audio: $oldAudioWav"
    Write-Host "mnemos log: $mnemosLog"
    Write-Host "audio log: $audioLog"
    exit 1
}

Write-Host "HSF2 CPS2 audio oracle: PASS"
Write-Host ("frames={0} cycles={1}" -f $Frames, $cycles)
Write-Host ("z80 pc={0:X4} af={1:X4}" -f $z80["PC"], $z80["AF"])
Write-Host ("qsound port_writes={0} register_writes={1} adpcm_triggers={2} last={3:X2}/{4:X4}" -f `
    $dl1425["PORTWR"], $dl1425["REGWR"], $dl1425["ADPCM_TRIG"], $dl1425["LASTREG"], $dl1425["LASTDATA"])
Write-Host ("audio old first/last/peak={0}/{1}/{2} mnemos significant first/last/peak={3}/{4}/{5} threshold={6}" -f `
    $oldWav.FirstSignificantEmulatedFrame, `
    $oldWav.LastSignificantEmulatedFrame, `
    $oldWav.Peak, `
    $wav.FirstSignificantEmulatedFrame, `
    $wav.LastSignificantEmulatedFrame, `
    $wav.Peak, `
    $AudioSignificantThreshold)
Write-Host ("audio old wav rate/frames={0}/{1} mnemos wav rate/frames={2}/{3}" -f `
    $oldWav.SampleRate, $oldWav.AudioFrames, $wav.SampleRate, $wav.AudioFrames)
Write-Host ("audio old raw first/last/nonzero_frames={0}/{1}/{2} mnemos raw first/last/nonzero_samples={3}/{4}/{5}" -f `
    $oldWav.FirstNonzeroEmulatedFrame, $oldWav.LastNonzeroEmulatedFrame, $oldWav.NonzeroSampleFrames, `
    $wav.FirstNonzeroEmulatedFrame, $wav.LastNonzeroEmulatedFrame, $wav.NonzeroSamples)
Write-Host "artifacts: $outPath"
