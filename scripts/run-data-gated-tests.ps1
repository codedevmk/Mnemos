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
#   MNEMOS_M72_RTYPE_SET      an R-Type-family M72 zip or directory -> irem_m72 golden/corpus smoke
#   MNEMOS_M72_PROTECTED_SET  a protected true-M72 zip or directory -> irem_m72 golden/corpus smoke
#   MNEMOS_M72_PROTECTED_AUDIO_SET  protected true-M72 audio-proof set -> irem_m72 rendered-audio golden
#   MNEMOS_M72_PROTECTED_MCU_SET  protected true-M72 set with dumped MCU -> irem_m72 dumped-MCU golden/corpus smoke
#   MNEMOS_M72_VERTICAL_SET   a vertical true-M72 zip or directory  -> irem_m72 golden/corpus smoke
#   MNEMOS_M72_SET_DIR        path-list of mixed roots or dirs with true-M72 zips/dirs/wrappers -> irem_m72 roster/corpus smoke
#   MNEMOS_M15_SET_DIR        path-list of dirs with M15 zips/dirs/wrappers -> irem_m15 manifest/player smoke
#   MNEMOS_M52_SET_DIR        path-list of dirs with M52 zips/dirs/wrappers -> irem_m52 manifest/player smoke
#   MNEMOS_M75_SET_DIR        path-list of dirs with M75 zips/dirs/wrappers -> irem_m75 manifest/player smoke
#   MNEMOS_M81_SET_DIR        path-list of dirs with M81 zips/dirs/wrappers -> irem_m81 manifest/player smoke
#   MNEMOS_M82_SET_DIR        path-list of dirs with M82 R-Type II zips/dirs/wrappers -> irem_m82 smoke
#   MNEMOS_M84_SET_DIR        path-list of dirs with M84 zips/dirs/wrappers plus M81 parent -> irem_m84 manifest/player smoke
#   MNEMOS_M90_SET_DIR        path-list of dirs with M90 zips/dirs/wrappers -> irem_m90 manifest/player smoke
#   MNEMOS_M92_SET_DIR        path-list of dirs with M92 zips/dirs/wrappers -> irem_m92 manifest/player smoke
#   MNEMOS_M107_SET_DIR       path-list of dirs with M107 zips/dirs/wrappers -> irem_m107 manifest/player smoke
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
    @{ Name = "MNEMOS_M72_RTYPE_SET";    Test = "irem_m72 golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_PROTECTED_SET"; Test = "irem_m72 golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_PROTECTED_AUDIO_SET"; Test = "irem_m72 rendered-audio golden" },
    @{ Name = "MNEMOS_M72_PROTECTED_MCU_SET"; Test = "irem_m72 dumped-MCU golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_VERTICAL_SET"; Test = "irem_m72 golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_SET_DIR";      Test = "irem_m72 roster/corpus smoke" },
    @{ Name = "MNEMOS_M15_SET_DIR";      Test = "irem_m15 manifest/player smoke" },
    @{ Name = "MNEMOS_M52_SET_DIR";      Test = "irem_m52 manifest/player smoke" },
    @{ Name = "MNEMOS_M75_SET_DIR";      Test = "irem_m75 manifest/player smoke" },
    @{ Name = "MNEMOS_M81_SET_DIR";      Test = "irem_m81 manifest/player smoke" },
    @{ Name = "MNEMOS_M82_SET_DIR";      Test = "irem_m82 R-Type II smoke" },
    @{ Name = "MNEMOS_M84_SET_DIR";      Test = "irem_m84 manifest/player smoke" },
    @{ Name = "MNEMOS_M90_SET_DIR";      Test = "irem_m90 manifest/player smoke" },
    @{ Name = "MNEMOS_M92_SET_DIR";      Test = "irem_m92 manifest/player smoke" },
    @{ Name = "MNEMOS_M107_SET_DIR";     Test = "irem_m107 manifest/player smoke" },
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
    -R "conformance|c64_basic_boot|sms_boot|genesis_boot|manifest_parity|mnemos_manifests_irem_m(15|52|75|81|82|84|90|92|107)_test|irem_m15_.*golden|irem_m52_.*golden|irem_m72_.*golden|irem_m75_.*golden|irem_m81_.*golden|irem_m82_.*golden|irem_m84_.*golden|irem_m90_.*golden|irem_m92_.*golden|irem_m107_.*golden"
$ctestExit = $LASTEXITCODE
if ($ctestExit -ne 0) {
    exit $ctestExit
}

$m72Runner = Join-Path $PSScriptRoot "irem_m72/run-corpus-smoke.ps1"
if (Test-Path $m72Runner) {
    Write-Host "`nRunning Irem M72 corpus smoke ..." -ForegroundColor Cyan
    & $m72Runner -BuildDir $BuildDir -Recurse
    $m72Exit = $LASTEXITCODE
    if ($m72Exit -ne 0) {
        exit $m72Exit
    }
}

$cps2Runner = Join-Path $PSScriptRoot "cps2/run-corpus-smoke.ps1"
if (Test-Path $cps2Runner) {
    Write-Host "`nRunning CPS2 corpus smoke ..." -ForegroundColor Cyan
    & $cps2Runner -BuildDir $BuildDir
    exit $LASTEXITCODE
}

exit 0
