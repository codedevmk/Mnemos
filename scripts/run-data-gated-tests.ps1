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
#   MNEMOS_TAITO_SET_DIR      broad Taito corpus root                -> Taito inventory + F2 smoke
#   MNEMOS_TAITO_GNET_PACKAGE one G-NET CHD package zip              -> G-NET media decode
#   MNEMOS_TAITO_GNET_BIOS    one G-NET/ZN BIOS ROM                  -> G-NET board/player smoke
#   MNEMOS_TAITO_F2_ROM       one Taito F2 ROM zip                   -> Taito F2 corpus smoke
#   MNEMOS_TAITO_F2_SET_DIR   dir with Taito F2 ROM zips             -> Taito F2 corpus smoke
#   MNEMOS_TAITO_F2_GUNFRONT_SET
#                             real gunfront/gunfrontj set/wrapper    -> taito_f2_adapter_test
#   MNEMOS_TAITO_F2_DINOREX_SET
#                             real dinorex.zip                       -> taito_f2_adapter_test
#
# Optional golden-hash pins (assert the rendered framebuffer once locked in):
#   MNEMOS_SMS_BOOT_SHA256, MNEMOS_C64_BOOT_SHA256, MNEMOS_GENESIS_BOOT_SHA256
#   MNEMOS_TAITO_REQUIRE_ALL_SUPPORTED=1 fails the broad Taito inventory if any
#                             package is not runnable by a Mnemos Taito adapter
#   MNEMOS_TAITO_F2_GOLDENS  JSON screenshot SHA-256 pins for Taito F2 corpus smoke
#   MNEMOS_TAITO_F2_REQUIRE_GOLDENS=1 fails Taito F2 sets that have no golden pin
#   MNEMOS_TAITO_F2_REQUIRE_MANIFEST_COVERAGE=1 fails if any checked-in F2
#                             manifest set is absent from the local corpus smoke
#   MNEMOS_TAITO_F2_AUDIO_PROBE=1 enables rendered WAV/audio JSON extraction
#                             during the Taito F2 corpus smoke
#   MNEMOS_TAITO_F2_AUDIO_FRAMES frame count for the F2 audio probe
#   MNEMOS_TAITO_F2_REQUIRE_AUDIO_EVIDENCE=1 fails an F2 set when the audio
#                             probe is absent, invalid, or silent
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
    @{ Name = "MNEMOS_CPS2_SET_DIR";     Test = "cps2 corpus smoke" },
    @{ Name = "MNEMOS_TAITO_SET_DIR";    Test = "Taito broad corpus inventory" },
    @{ Name = "MNEMOS_TAITO_GNET_PACKAGE"; Test = "Taito G-NET CHD package decode/player smoke" },
    @{ Name = "MNEMOS_TAITO_GNET_BIOS"; Test = "Taito G-NET board/player smoke" },
    @{ Name = "MNEMOS_TAITO_F2_ROM";     Test = "Taito F2 corpus smoke" },
    @{ Name = "MNEMOS_TAITO_F2_SET_DIR"; Test = "Taito F2 corpus smoke" },
    @{ Name = "MNEMOS_TAITO_F2_GUNFRONT_SET"; Test = "taito_f2_adapter_test (real Gun Frontier)" },
    @{ Name = "MNEMOS_TAITO_F2_DINOREX_SET"; Test = "taito_f2_adapter_test (real Dino Rex)" },
    @{ Name = "MNEMOS_TAITO_F2_GOLDENS"; Test = "Taito F2 screenshot golden pins" },
    @{ Name = "MNEMOS_TAITO_F2_REQUIRE_MANIFEST_COVERAGE"; Test = "Taito F2 manifest coverage enforcement" },
    @{ Name = "MNEMOS_TAITO_F2_AUDIO_PROBE"; Test = "Taito F2 rendered audio probe" },
    @{ Name = "MNEMOS_TAITO_F2_REQUIRE_AUDIO_EVIDENCE"; Test = "Taito F2 audio evidence enforcement" }
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
    $cps2Exit = $LASTEXITCODE
    if ($cps2Exit -ne 0) {
        exit $cps2Exit
    }
}

$taitoInventory = Join-Path $PSScriptRoot "taito/inventory-corpus.ps1"
$taitoSetDir = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_SET_DIR")
if ((Test-Path $taitoInventory) -and -not [string]::IsNullOrWhiteSpace($taitoSetDir)) {
    Write-Host "`nRunning Taito corpus inventory ..." -ForegroundColor Cyan
    $inventoryArgs = @{
        RomDir = $taitoSetDir
        Recurse = $true
    }
    $requireAllTaito = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_REQUIRE_ALL_SUPPORTED")
    if (-not [string]::IsNullOrWhiteSpace($requireAllTaito) -and
        $requireAllTaito -match '^(1|true|yes|on)$') {
        $inventoryArgs.RequireAllSupported = $true
    }
    & $taitoInventory @inventoryArgs
    $inventoryExit = $LASTEXITCODE
    if ($inventoryExit -ne 0) {
        exit $inventoryExit
    }
}

$gnetPackage = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_GNET_PACKAGE")
if ([string]::IsNullOrWhiteSpace($gnetPackage) -and
    -not [string]::IsNullOrWhiteSpace($taitoSetDir)) {
    foreach ($candidate in @("chaoshea.zip", "gobyrc.zip", "raycris.zip", "sianniv.zip",
            "spuzbobl.zip")) {
        $candidatePath = Join-Path $taitoSetDir $candidate
        if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
            $gnetPackage = $candidatePath
            break
        }
    }
}
if (-not [string]::IsNullOrWhiteSpace($gnetPackage) -and
    (Test-Path -LiteralPath $gnetPackage -PathType Leaf)) {
    Write-Host "`nRunning Taito G-NET media/system/adapter smoke ..." -ForegroundColor Cyan
    $previousGnetPackage = [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_GNET_PACKAGE")
    [Environment]::SetEnvironmentVariable("MNEMOS_TAITO_GNET_PACKAGE", $gnetPackage, "Process")
    ctest --test-dir $testDir --output-on-failure -R "taito_gnet_(media|system|adapter)|system_launch"
    $gnetExit = $LASTEXITCODE
    [Environment]::SetEnvironmentVariable(
        "MNEMOS_TAITO_GNET_PACKAGE", $previousGnetPackage, "Process")
    if ($gnetExit -ne 0) {
        exit $gnetExit
    }
}

$taitoF2Runner = Join-Path $PSScriptRoot "taito-f2/run-corpus-smoke.ps1"
if (Test-Path $taitoF2Runner) {
    Write-Host "`nRunning Taito F2 corpus smoke ..." -ForegroundColor Cyan
    $taitoF2Args = @{
        BuildDir = $BuildDir
    }
    if (-not [string]::IsNullOrWhiteSpace($taitoSetDir)) {
        $taitoF2Args.RomDir = $taitoSetDir
        $taitoF2Args.Recurse = $true
    }
    $requireF2ManifestCoverage =
        [Environment]::GetEnvironmentVariable("MNEMOS_TAITO_F2_REQUIRE_MANIFEST_COVERAGE")
    if (-not [string]::IsNullOrWhiteSpace($requireF2ManifestCoverage) -and
        $requireF2ManifestCoverage -match '^(1|true|yes|on)$') {
        $taitoF2Args.RequireManifestCoverage = $true
    }
    & $taitoF2Runner @taitoF2Args
    exit $LASTEXITCODE
}

exit 0
