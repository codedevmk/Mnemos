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
#
# Optional golden-hash pins (assert the rendered framebuffer once locked in):
#   MNEMOS_SMS_BOOT_SHA256, MNEMOS_C64_BOOT_SHA256, MNEMOS_GENESIS_BOOT_SHA256
#
# This script dot-sources scripts/local-roms.ps1 if present so you can keep your
# machine-specific paths there (that file is git-ignored). Nothing here copies a ROM
# into the repository.

param(
    [string]$BuildDir = "build/windows-msvc-debug"
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
    @{ Name = "MNEMOS_CPS2_SET_DIR";     Test = "cps2 corpus smoke" }
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
    -R "conformance|c64_basic_boot|sms_boot|genesis_boot|manifest_parity"
$ctestExit = $LASTEXITCODE
if ($ctestExit -ne 0) {
    exit $ctestExit
}

$cps2Runner = Join-Path $PSScriptRoot "cps2/run-corpus-smoke.ps1"
if (Test-Path $cps2Runner) {
    Write-Host "`nRunning CPS2 corpus smoke ..." -ForegroundColor Cyan
    & $cps2Runner -BuildDir $BuildDir
    exit $LASTEXITCODE
}

exit 0
