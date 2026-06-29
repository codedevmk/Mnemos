#!/usr/bin/env pwsh
# Run the data-gated tests against a local ROM / test-corpus library.
#
# Several tests need copyrighted material that is NEVER committed (system ROMs,
# cartridge images, conformance corpora). Each one self-SKIPs unless an
# environment variable points at the data (see THIRD-PARTY-REFERENCES.md for corpus refs):
#
#   MNEMOS_SMS_ROM            an SMS cartridge image                  -> sms_boot_test
#   MNEMOS_C64_ROM_DIR        dir with the three C64 system ROMs      -> c64_basic_boot_test
#   MNEMOS_GENESIS_ROM        a Genesis cartridge image               -> genesis_boot_test
#   MNEMOS_Z80_TEST_ROM       a CP/M .com Z80 exerciser image         -> z80_conformance_test
#   MNEMOS_M6510_TESTS_DIR    per-instruction 6502 test JSON dir      -> m6510_conformance_test
#   MNEMOS_M68000_TESTS_DIR   per-instruction 68000 test JSON dir     -> m68000_conformance_test
#   MNEMOS_SMS_BIOS           an SMS BIOS/cart image (A/B comparator) -> sms_manifest_parity_test
#   MNEMOS_CPS2_ROM           one CPS2 ROM/key zip                   -> cps2 corpus smoke
#   MNEMOS_CPS2_SET_DIR       dir with CPS2 ROM/key zips             -> cps2 corpus smoke
#   MNEMOS_MSX_BIOS           an MSX BIOS image                      -> msx boot smoke
#   MNEMOS_MSX_ROM            an MSX cartridge image                 -> msx boot smoke
#   MNEMOS_MSX_ROM2           second MSX cartridge image             -> msx boot smoke
#   MNEMOS_MSX_DISK_ROM       optional MSX disk interface ROM        -> msx boot smoke
#   MNEMOS_MSX_DSK            optional flat MSX DSK image            -> msx boot smoke
#   MNEMOS_MSX_CAS            optional MSX CAS tape image            -> msx boot smoke
#   MNEMOS_MSX_KANJI_ROM      optional MSX Kanji ROM image           -> msx boot smoke
#   MNEMOS_MSX_MAPPER         MSX cartridge mapper override          -> msx boot smoke
#   MNEMOS_MSX_MAPPER2        second MSX cartridge mapper override   -> msx boot smoke
#   MNEMOS_MSX_EXPANDED_SLOTS expanded primary-slot mask/list        -> msx boot smoke
#   MNEMOS_MSX_RAM_SLOT       RAM slot primary[.secondary]           -> msx boot smoke
#   MNEMOS_MSX_RAM_SIZE       mapper RAM size in bytes/K/M           -> msx boot smoke
#   MNEMOS_MSX_DISK_SLOT      disk ROM slot primary[.secondary]      -> msx boot smoke
#   MNEMOS_MSX_CART2_SLOT     second cart slot primary[.secondary]   -> msx boot smoke
#   MNEMOS_MSX_ROM_DIR        dir with MSX cartridge images          -> msx boot smoke
#   MNEMOS_MSX2_FIRMWARE      packed MSX2 BIOS/sub-ROM image         -> msx2 boot smoke
#   MNEMOS_MSX2_BIOS          split main BIOS or packed BIOS image    -> msx2 boot smoke
#   MNEMOS_MSX2_SUB_ROM       split MSX2 sub-ROM image                -> msx2 boot smoke
#   MNEMOS_MSX2_LOGO_ROM      optional MSX2 logo ROM image            -> msx2 boot smoke
#   MNEMOS_MSX2_ROM           an MSX2 cartridge image                -> msx2 boot smoke
#   MNEMOS_MSX2_ROM2          second MSX2 cartridge image            -> msx2 boot smoke
#   MNEMOS_MSX2_DISK_ROM      optional MSX2 disk interface ROM       -> msx2 boot smoke
#   MNEMOS_MSX2_DSK           optional flat MSX DSK image            -> msx2 boot smoke
#   MNEMOS_MSX2_CAS           optional MSX CAS tape image            -> msx2 boot smoke
#   MNEMOS_MSX2_KANJI_ROM     optional MSX2 Kanji ROM image          -> msx2 boot smoke
#   MNEMOS_MSX2_MAPPER        MSX2 cartridge mapper override         -> msx2 boot smoke
#   MNEMOS_MSX2_MAPPER2       second MSX2 cartridge mapper override  -> msx2 boot smoke
#   MNEMOS_MSX2_EXPANDED_SLOTS expanded primary-slot mask/list       -> msx2 boot smoke
#   MNEMOS_MSX2_RAM_SLOT      RAM slot primary[.secondary]           -> msx2 boot smoke
#   MNEMOS_MSX2_SUB_SLOT      sub-ROM slot primary[.secondary]       -> msx2 boot smoke
#   MNEMOS_MSX2_DISK_SLOT     disk ROM slot primary[.secondary]      -> msx2 boot smoke
#   MNEMOS_MSX2_CART2_SLOT    second cart slot primary[.secondary]   -> msx2 boot smoke
#   MNEMOS_MSX2_RAM_SIZE      mapper RAM size in bytes/K/M           -> msx2 boot smoke
#   MNEMOS_MSX2_ROM_DIR       dir with MSX2 cartridge images         -> msx2 boot smoke
#   MNEMOS_MSX_CASE_MANIFEST  mixed MSX/MSX2 smoke cases             -> msx/msx2 boot smoke
#
# Optional golden-hash pins (assert the rendered framebuffer once locked in):
#   MNEMOS_SMS_BOOT_SHA256, MNEMOS_C64_BOOT_SHA256, MNEMOS_GENESIS_BOOT_SHA256
#   MNEMOS_CPS2_FRAME_HASH_BASELINE overrides the default CPS2 CSV baseline.
#   MNEMOS_CPS2_AUDIO_FRAMES extends only the CPS2 rendered-audio proof window.
#   MNEMOS_CPS2_AUDIO_SIGNIFICANT_THRESHOLD overrides the audio significance floor.
#   MNEMOS_CPS2_AUDIO_STATE_PROBE=1 records final audio-window QSound counters.
#   MNEMOS_CPS2_GAMEPLAY_INPUTS=1 uses the CPS2 coin/start/fire gameplay probe.
#   MNEMOS_CPS2_GAMEPLAY_PLAYERS selects 1-4 player ports for gameplay probes.
#   MNEMOS_CPS2_GAMEPLAY_REPEAT=1 repeats coin/start pulses every 300 frames.
#   MNEMOS_CPS2_GAMEPLAY_SAVE_INPUTS=1 also applies gameplay inputs to screenshots.
#   MNEMOS_CPS2_ONLY_SETS comma/semicolon list for focused CPS2 corpus probes.
#   MNEMOS_CPS2_SKIP_SETS comma/semicolon list of CPS2 sets to skip.
#   MNEMOS_CPS2_START_AFTER resumes after one CPS2 set id or zip name.
#   MNEMOS_MSX_BOOT_SHA256, MNEMOS_MSX2_BOOT_SHA256
#
# This script dot-sources scripts/local-roms.ps1 if present so you can keep your
# machine-specific paths there (that file is git-ignored). Nothing here copies a ROM
# into the repository.
# Use -RequireMsxData for MSX/MSX2 proof runs that must fail when real firmware
# or manifest cases are not configured.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [switch]$RequireMsxData
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$localConfig = Join-Path $PSScriptRoot "local-roms.ps1"
if (Test-Path $localConfig) {
    Write-Host "Loading local ROM paths from $localConfig" -ForegroundColor Cyan
    . $localConfig
} else {
    Write-Host "No scripts/local-roms.ps1 found; relying on already-set environment variables." -ForegroundColor Yellow
}

# Report what is wired vs. what will skip.
$vars = @(
    @{ Name = "MNEMOS_SMS_ROM";          Test = "sms_boot_test" },
    @{ Name = "MNEMOS_C64_ROM_DIR";      Test = "c64_basic_boot_test" },
    @{ Name = "MNEMOS_GENESIS_ROM";      Test = "genesis_boot_test" },
    @{ Name = "MNEMOS_Z80_TEST_ROM";     Test = "z80_conformance_test" },
    @{ Name = "MNEMOS_M6510_TESTS_DIR";  Test = "m6510_conformance_test" },
    @{ Name = "MNEMOS_M68000_TESTS_DIR"; Test = "m68000_conformance_test" },
    @{ Name = "MNEMOS_SMS_BIOS";         Test = "sms_manifest_parity_test" },
    @{ Name = "MNEMOS_32X_BIOS_DIR";     Test = "sega32x_adapter_test (boot golden)" },
    @{ Name = "MNEMOS_32X_ROM";          Test = "sega32x_adapter_test (boot golden)" },
    @{ Name = "MNEMOS_CPS2_ROM";         Test = "cps2 corpus smoke" },
    @{ Name = "MNEMOS_CPS2_SET_DIR";     Test = "cps2 corpus smoke" },
    @{ Name = "MNEMOS_MSX_BIOS";         Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_ROM";          Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_ROM2";         Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_DISK_ROM";     Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_DSK";          Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_CAS";          Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_KANJI_ROM";    Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_MAPPER";       Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_MAPPER2";      Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_EXPANDED_SLOTS"; Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_RAM_SLOT";     Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_RAM_SIZE";     Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_DISK_SLOT";    Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_CART2_SLOT";   Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX_ROM_DIR";      Test = "msx boot smoke" },
    @{ Name = "MNEMOS_MSX2_FIRMWARE";    Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_BIOS";        Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_SUB_ROM";     Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_LOGO_ROM";    Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_ROM";         Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_ROM2";        Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_DISK_ROM";    Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_DSK";         Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_CAS";         Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_KANJI_ROM";   Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_MAPPER";      Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_MAPPER2";     Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_EXPANDED_SLOTS"; Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_RAM_SLOT";    Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_SUB_SLOT";    Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_DISK_SLOT";   Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_CART2_SLOT";  Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_RAM_SIZE";    Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX2_ROM_DIR";     Test = "msx2 boot smoke" },
    @{ Name = "MNEMOS_MSX_CASE_MANIFEST"; Test = "msx/msx2 boot smoke" }
)
foreach ($v in $vars) {
    $value = [Environment]::GetEnvironmentVariable($v.Name)
    if ([string]::IsNullOrEmpty($value)) {
        Write-Host ("  [skip] {0,-26} {1} (unset)" -f $v.Name, $v.Test) -ForegroundColor DarkGray
    } else {
        Write-Host ("  [wired] {0,-25} {1}" -f $v.Name, $v.Test) -ForegroundColor Green
    }
}

$testDir = Join-Path $repoRoot $BuildDir
if (-not (Test-Path $testDir)) {
    throw "Build directory '$testDir' not found. Configure/build first, or pass -BuildDir."
}

Write-Host "`nRunning data-gated tests in $testDir ..." -ForegroundColor Cyan
ctest --test-dir $testDir --output-on-failure `
    -R "conformance|c64_basic_boot|sms_boot|genesis_boot|msx_boot|manifest_parity"
$ctestExit = $LASTEXITCODE
if ($ctestExit -ne 0) {
    exit $ctestExit
}

$runnerExit = 0

$cps2Runner = Join-Path $PSScriptRoot "cps2/run-corpus-smoke.ps1"
if (Test-Path $cps2Runner) {
    Write-Host "`nRunning CPS2 corpus smoke ..." -ForegroundColor Cyan
    $cps2Args = @{ BuildDir = $BuildDir }
    $cps2AudioFrames = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_AUDIO_FRAMES")
    $cps2AudioSignificantThreshold =
        [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_AUDIO_SIGNIFICANT_THRESHOLD")
    $cps2AudioStateProbe = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_AUDIO_STATE_PROBE")
    $cps2GameplayInputs = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_GAMEPLAY_INPUTS")
    $cps2GameplayPlayers = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_GAMEPLAY_PLAYERS")
    $cps2GameplayRepeat = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_GAMEPLAY_REPEAT")
    $cps2GameplaySaveInputs = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_GAMEPLAY_SAVE_INPUTS")
    $cps2OnlySets = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_ONLY_SETS")
    $cps2SkipSets = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_SKIP_SETS")
    $cps2StartAfter = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_START_AFTER")
    $useCps2AudioStateProbe = ($cps2AudioStateProbe -eq "1" -or
        $cps2AudioStateProbe -eq "true" -or
        $cps2AudioStateProbe -eq "TRUE")
    $useCps2GameplayInputs = ($cps2GameplayInputs -eq "1" -or
        $cps2GameplayInputs -eq "true" -or
        $cps2GameplayInputs -eq "TRUE")
    $useCps2GameplayRepeat = ($cps2GameplayRepeat -eq "1" -or
        $cps2GameplayRepeat -eq "true" -or
        $cps2GameplayRepeat -eq "TRUE")
    $useCps2GameplaySaveInputs = ($cps2GameplaySaveInputs -eq "1" -or
        $cps2GameplaySaveInputs -eq "true" -or
        $cps2GameplaySaveInputs -eq "TRUE")
    $usesCps2FilteredOrAlternateProof = (-not [string]::IsNullOrWhiteSpace($cps2AudioFrames) -or
        -not [string]::IsNullOrWhiteSpace($cps2AudioSignificantThreshold) -or
        $useCps2AudioStateProbe -or
        $useCps2GameplayInputs -or
        -not [string]::IsNullOrWhiteSpace($cps2GameplayPlayers) -or
        $useCps2GameplayRepeat -or
        $useCps2GameplaySaveInputs -or
        -not [string]::IsNullOrWhiteSpace($cps2OnlySets) -or
        -not [string]::IsNullOrWhiteSpace($cps2SkipSets) -or
        -not [string]::IsNullOrWhiteSpace($cps2StartAfter))
    $cps2Baseline = [Environment]::GetEnvironmentVariable("MNEMOS_CPS2_FRAME_HASH_BASELINE")
    if ([string]::IsNullOrWhiteSpace($cps2Baseline)) {
        $defaultBaseline = Join-Path $repoRoot "tests/golden/cps2_frame_hash_baseline.csv"
        if ((Test-Path -LiteralPath $defaultBaseline -PathType Leaf) -and
            -not $usesCps2FilteredOrAlternateProof -and
            -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable("MNEMOS_CPS2_SET_DIR"))) {
            $cps2Baseline = $defaultBaseline
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($cps2Baseline)) {
        Write-Host "  using CPS2 frame baseline: $cps2Baseline" -ForegroundColor DarkGray
        $cps2Args.ExpectedFrameHashes = $cps2Baseline
    }
    if (-not [string]::IsNullOrWhiteSpace($cps2AudioFrames)) {
        $parsedAudioFrames = 0
        if ([int]::TryParse($cps2AudioFrames, [ref]$parsedAudioFrames) -and
            $parsedAudioFrames -gt 0) {
            Write-Host "  using CPS2 audio frames: $parsedAudioFrames" -ForegroundColor DarkGray
            $cps2Args.AudioFrames = $parsedAudioFrames
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($cps2AudioSignificantThreshold)) {
        $parsedAudioSignificantThreshold = 0
        if ([int]::TryParse($cps2AudioSignificantThreshold,
                            [ref]$parsedAudioSignificantThreshold) -and
            $parsedAudioSignificantThreshold -gt 0) {
            Write-Host "  using CPS2 audio significance threshold: $parsedAudioSignificantThreshold" -ForegroundColor DarkGray
            $cps2Args.AudioSignificantThreshold = $parsedAudioSignificantThreshold
        }
    }
    if ($useCps2AudioStateProbe) {
        Write-Host "  recording CPS2 audio-window QSound state" -ForegroundColor DarkGray
        $cps2Args.AudioStateProbe = $true
    }
    if ($useCps2GameplayInputs) {
        Write-Host "  using CPS2 gameplay input probe" -ForegroundColor DarkGray
        $cps2Args.GameplayInput = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($cps2GameplayPlayers)) {
        $parsedGameplayPlayers = 0
        if ([int]::TryParse($cps2GameplayPlayers, [ref]$parsedGameplayPlayers) -and
            $parsedGameplayPlayers -ge 1 -and
            $parsedGameplayPlayers -le 4) {
            Write-Host "  using CPS2 gameplay players: $parsedGameplayPlayers" -ForegroundColor DarkGray
            $cps2Args.GameplayPlayers = $parsedGameplayPlayers
        }
    }
    if ($useCps2GameplayRepeat) {
        Write-Host "  repeating CPS2 gameplay coin/start pulses" -ForegroundColor DarkGray
        $cps2Args.GameplayRepeat = $true
    }
    if ($useCps2GameplaySaveInputs) {
        Write-Host "  using CPS2 gameplay input probe for save/load screenshots" -ForegroundColor DarkGray
        $cps2Args.GameplaySaveInput = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($cps2OnlySets)) {
        Write-Host "  using CPS2 only-set filter: $cps2OnlySets" -ForegroundColor DarkGray
        $cps2Args.OnlySets = $cps2OnlySets
    }
    if (-not [string]::IsNullOrWhiteSpace($cps2SkipSets)) {
        Write-Host "  using CPS2 skip-set filter: $cps2SkipSets" -ForegroundColor DarkGray
        $cps2Args.SkipSets = $cps2SkipSets
    }
    if (-not [string]::IsNullOrWhiteSpace($cps2StartAfter)) {
        Write-Host "  resuming CPS2 corpus after: $cps2StartAfter" -ForegroundColor DarkGray
        $cps2Args.StartAfter = $cps2StartAfter
    }
    & $cps2Runner @cps2Args
    if ($LASTEXITCODE -ne 0) {
        $runnerExit = $LASTEXITCODE
    }
}

$msxRunner = Join-Path $PSScriptRoot "msx/run-boot-smoke.ps1"
if (Test-Path $msxRunner) {
    Write-Host "`nRunning MSX/MSX2 boot smoke ..." -ForegroundColor Cyan
    $msxArgs = @{ BuildDir = $BuildDir }
    if ($RequireMsxData) {
        $msxArgs.RequireData = $true
    }
    & $msxRunner @msxArgs
    if ($LASTEXITCODE -ne 0 -and $runnerExit -eq 0) {
        $runnerExit = $LASTEXITCODE
    }
}

exit $runnerExit
