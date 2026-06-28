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
#   MNEMOS_M72_RTYPE_SET      R-Type-family M72 zip/dir/path-list -> irem_m72 golden/corpus smoke
#   MNEMOS_M72_PROTECTED_SET  protected true-M72 zip/dir/path-list -> irem_m72 golden/corpus smoke
#   MNEMOS_M72_PROTECTED_AUDIO_SET  protected true-M72 audio-proof zip/dir/path-list -> irem_m72 rendered-audio golden/corpus smoke
#   MNEMOS_M72_PROTECTED_MCU_SET  protected true-M72 zip/dir/path-list with dumped MCU -> irem_m72 dumped-MCU golden/corpus smoke
#   MNEMOS_M72_PARITY_SET     reference-captured M72 zip/dir/path-list -> irem_m72 visual/audio parity hash golden
#   MNEMOS_M72_PARITY_FRAME_SHA256  expected final RGBA framebuffer SHA-256 for MNEMOS_M72_PARITY_SET
#   MNEMOS_M72_PARITY_AUDIO_SHA256  expected interleaved s16le rendered-audio SHA-256 for MNEMOS_M72_PARITY_SET
#   MNEMOS_M72_PARITY_FRAMES  frame count for the M72 parity hash golden (default: 600)
#   MNEMOS_M72_VERTICAL_SET   vertical true-M72 zip/dir/path-list -> irem_m72 golden/corpus smoke
#   MNEMOS_M72_SET_DIR        path-list of mixed roots or dirs with true-M72 zips/dirs/wrappers -> irem_m72 roster/corpus smoke
#   MNEMOS_M14_SET_DIR        path-list of dirs with M14 zips/dirs/wrappers -> irem_m14 manifest/player smoke
#   MNEMOS_M15_SET_DIR        path-list of dirs with M15 zips/dirs/wrappers -> irem_m15 manifest/player smoke
#   MNEMOS_M27_SET_DIR        path-list of dirs with M27 zips/dirs/wrappers -> irem_m27 manifest/player smoke
#   MNEMOS_M47_SET_DIR        path-list of dirs with M47 zips/dirs/wrappers -> irem_m47 manifest/player smoke
#   MNEMOS_M52_SET_DIR        path-list of dirs with M52 zips/dirs/wrappers -> irem_m52 manifest/player smoke
#   MNEMOS_M52_PARITY_SET     a reference-captured M52 zip or directory -> irem_m52 visual/audio parity hash golden
#   MNEMOS_M52_PARITY_FRAME_SHA256  expected final RGBA framebuffer SHA-256 for MNEMOS_M52_PARITY_SET
#   MNEMOS_M52_PARITY_AUDIO_SHA256  expected interleaved s16le rendered-audio SHA-256 for MNEMOS_M52_PARITY_SET
#   MNEMOS_M52_PARITY_FRAMES  frame count for the M52 parity hash golden (default: 600)
#   MNEMOS_M57_SET_DIR        path-list of dirs with M57 zips/dirs/wrappers -> irem_m57 manifest/player smoke
#   MNEMOS_M58_SET_DIR        path-list of dirs with M58 zips/dirs/wrappers -> irem_m58 manifest/player smoke
#   MNEMOS_M58_PARITY_SET     a reference-captured M58 zip or directory -> irem_m58 visual/audio parity hash golden
#   MNEMOS_M58_PARITY_FRAME_SHA256  expected final RGBA framebuffer SHA-256 for MNEMOS_M58_PARITY_SET
#   MNEMOS_M58_PARITY_AUDIO_SHA256  expected interleaved s16le rendered-audio SHA-256 for MNEMOS_M58_PARITY_SET
#   MNEMOS_M58_PARITY_FRAMES  frame count for the M58 parity hash golden (default: 600)
#   MNEMOS_M62_SET_DIR        path-list of dirs with M62 zips/dirs/wrappers -> irem_m62 manifest/player smoke
#   MNEMOS_M63_SET_DIR        path-list of dirs with M63 zips/dirs/wrappers -> irem_m63 manifest/player smoke
#   MNEMOS_TRAVRUSA_SET_DIR   path-list of dirs with travrusa zips/wrappers -> irem_travrusa manifest/player smoke
#   MNEMOS_M75_SET_DIR        path-list of dirs with M75 zips/dirs/wrappers -> irem_m75 manifest/player smoke
#   MNEMOS_M81_SET_DIR        path-list of dirs with M81 zips/dirs/wrappers -> irem_m81 manifest/player smoke
#   MNEMOS_M82_SET_DIR        path-list of dirs with M82 R-Type II zips/dirs/wrappers -> irem_m82 smoke
#   MNEMOS_M84_SET_DIR        path-list of dirs with M84 zips/dirs/wrappers plus M81 parent -> irem_m84 manifest/player smoke
#   MNEMOS_M85_SET_DIR        path-list of dirs with M85 zips/dirs/wrappers -> irem_m85 manifest/player smoke
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
    @{ Name = "MNEMOS_M72_PARITY_SET"; Test = "irem_m72 visual/audio parity hash golden" },
    @{ Name = "MNEMOS_M72_VERTICAL_SET"; Test = "irem_m72 golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_SET_DIR";      Test = "irem_m72 roster/corpus smoke" },
    @{ Name = "MNEMOS_M14_SET_DIR";      Test = "irem_m14 manifest/player smoke" },
    @{ Name = "MNEMOS_M15_SET_DIR";      Test = "irem_m15 manifest/player smoke" },
    @{ Name = "MNEMOS_M27_SET_DIR";      Test = "irem_m27 manifest/player smoke" },
    @{ Name = "MNEMOS_M47_SET_DIR";      Test = "irem_m47 manifest/player smoke" },
    @{ Name = "MNEMOS_M52_SET_DIR";      Test = "irem_m52 manifest/player smoke" },
    @{ Name = "MNEMOS_M52_PARITY_SET"; Test = "irem_m52 visual/audio parity hash golden" },
    @{ Name = "MNEMOS_M57_SET_DIR";      Test = "irem_m57 manifest/player smoke" },
    @{ Name = "MNEMOS_M58_SET_DIR";      Test = "irem_m58 manifest/player smoke" },
    @{ Name = "MNEMOS_M58_PARITY_SET"; Test = "irem_m58 visual/audio parity hash golden" },
    @{ Name = "MNEMOS_M62_SET_DIR";      Test = "irem_m62 manifest/player smoke" },
    @{ Name = "MNEMOS_M63_SET_DIR";      Test = "irem_m63 manifest/player smoke" },
    @{ Name = "MNEMOS_TRAVRUSA_SET_DIR"; Test = "irem_travrusa manifest/player smoke" },
    @{ Name = "MNEMOS_M75_SET_DIR";      Test = "irem_m75 manifest/player smoke" },
    @{ Name = "MNEMOS_M81_SET_DIR";      Test = "irem_m81 manifest/player smoke" },
    @{ Name = "MNEMOS_M82_SET_DIR";      Test = "irem_m82 R-Type II smoke" },
    @{ Name = "MNEMOS_M84_SET_DIR";      Test = "irem_m84 manifest/player smoke" },
    @{ Name = "MNEMOS_M85_SET_DIR";      Test = "irem_m85 manifest/player smoke" },
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
    -R "conformance|c64_basic_boot|sms_boot|genesis_boot|manifest_parity|mnemos_manifests_irem_m(14|15|27|47|52|57|58|62|63|75|81|82|84|85|90|92|107)_test|mnemos_manifests_irem_m14_system_test|mnemos_manifests_irem_m27_system_test|mnemos_manifests_irem_m47_system_test|mnemos_manifests_irem_m57_system_test|mnemos_manifests_irem_m62_system_test|mnemos_manifests_irem_m63_system_test|mnemos_manifests_irem_travrusa_test|irem_m14_.*golden|irem_m15_.*golden|irem_m27_.*golden|irem_m47_.*golden|irem_m52_.*golden|irem_m57_.*golden|irem_m58_.*golden|irem_m62_.*golden|irem_m63_.*golden|irem_m72_.*golden|irem_m75_.*golden|irem_m81_.*golden|irem_m82_.*golden|irem_m84_.*golden|irem_m85_.*golden|irem_m90_.*golden|irem_m92_.*golden|irem_m107_.*golden|irem_travrusa_.*golden"
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
