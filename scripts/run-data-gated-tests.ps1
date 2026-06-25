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
#   MNEMOS_SMS_BOOT_SHA256, MNEMOS_C64_BOOT_SHA256, MNEMOS_GENESIS_BOOT_SHA256,
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
    & $cps2Runner -BuildDir $BuildDir
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
