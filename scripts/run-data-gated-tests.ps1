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
#   MNEMOS_M10_SET_DIR        path-list of dirs with M10/M11 zips/dirs/wrappers -> irem_m10 manifest/player smoke
#   MNEMOS_M14_SET_DIR        path-list of dirs with M14 zips/dirs/wrappers -> irem_m14 manifest/player smoke
#   MNEMOS_M15_SET_DIR        path-list of dirs with M15 zips/dirs/wrappers -> irem_m15 manifest/player smoke
#   MNEMOS_M27_SET_DIR        path-list of dirs with M27 zips/dirs/wrappers -> irem_m27 manifest/player smoke
#   MNEMOS_REDALERT_SET_DIR   path-list of dirs with Red Alert/WW III zips/dirs/wrappers -> irem_redalert manifest/player smoke
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
#   MNEMOS_M78_SET_DIR        path-list of dirs with M78 zips/dirs/wrappers -> irem_m78 manifest/player smoke
#   MNEMOS_M81_SET_DIR        path-list of dirs with M81 zips/dirs/wrappers -> irem_m81 manifest/player smoke
#   MNEMOS_M82_SET_DIR        path-list of dirs with M82 R-Type II zips/dirs/wrappers -> irem_m82 smoke
#   MNEMOS_M84_SET_DIR        path-list of dirs with M84 zips/dirs/wrappers plus M81 parent -> irem_m84 manifest/player smoke
#   MNEMOS_M85_SET_DIR        path-list of dirs with M85 zips/dirs/wrappers -> irem_m85 manifest/player smoke
#   MNEMOS_M90_SET_DIR        path-list of dirs with M90 zips/dirs/wrappers -> irem_m90 manifest/player smoke
#   MNEMOS_M92_SET_DIR        path-list of dirs with M92 zips/dirs/wrappers -> irem_m92 manifest/player smoke
#   MNEMOS_M102_SET_DIR       path-list of dirs with M102 zips/dirs/wrappers -> irem_m102 manifest/player smoke
#   MNEMOS_M107_SET_DIR       path-list of dirs with M107 zips/dirs/wrappers -> irem_m107 manifest/player smoke
#   MNEMOS_M119_SET_DIR       path-list of dirs with M119 zips/dirs/wrappers -> irem_m119 manifest/player smoke
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
    @{ Name = "MNEMOS_M72_RTYPE_SET";    Test = "irem_m72 golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_PROTECTED_SET"; Test = "irem_m72 golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_PROTECTED_AUDIO_SET"; Test = "irem_m72 rendered-audio golden" },
    @{ Name = "MNEMOS_M72_PROTECTED_MCU_SET"; Test = "irem_m72 dumped-MCU golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_PARITY_SET"; Test = "irem_m72 visual/audio parity hash golden" },
    @{ Name = "MNEMOS_M72_VERTICAL_SET"; Test = "irem_m72 golden/corpus smoke" },
    @{ Name = "MNEMOS_M72_SET_DIR";      Test = "irem_m72 roster/corpus smoke" },
    @{ Name = "MNEMOS_M10_SET_DIR";      Test = "irem_m10 manifest/player smoke" },
    @{ Name = "MNEMOS_M14_SET_DIR";      Test = "irem_m14 manifest/player smoke" },
    @{ Name = "MNEMOS_M15_SET_DIR";      Test = "irem_m15 manifest/player smoke" },
    @{ Name = "MNEMOS_M27_SET_DIR";      Test = "irem_m27 manifest/player smoke" },
    @{ Name = "MNEMOS_REDALERT_SET_DIR"; Test = "irem_redalert manifest/player smoke" },
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
    @{ Name = "MNEMOS_M78_SET_DIR";      Test = "irem_m78 manifest/player smoke" },
    @{ Name = "MNEMOS_M81_SET_DIR";      Test = "irem_m81 manifest/player smoke" },
    @{ Name = "MNEMOS_M82_SET_DIR";      Test = "irem_m82 R-Type II smoke" },
    @{ Name = "MNEMOS_M84_SET_DIR";      Test = "irem_m84 manifest/player smoke" },
    @{ Name = "MNEMOS_M85_SET_DIR";      Test = "irem_m85 manifest/player smoke" },
    @{ Name = "MNEMOS_M90_SET_DIR";      Test = "irem_m90 manifest/player smoke" },
    @{ Name = "MNEMOS_M92_SET_DIR";      Test = "irem_m92 manifest/player smoke" },
    @{ Name = "MNEMOS_M102_SET_DIR";     Test = "irem_m102 manifest/player smoke" },
    @{ Name = "MNEMOS_M107_SET_DIR";     Test = "irem_m107 manifest/player smoke" },
    @{ Name = "MNEMOS_M119_SET_DIR";     Test = "irem_m119 manifest/player smoke" },
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
    @{ Name = "MNEMOS_MSX_CASE_MANIFEST"; Test = "msx/msx2 boot smoke" },
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
    -R "conformance|c64_basic_boot|sms_boot|genesis_boot|msx_boot|manifest_parity|mnemos_manifests_irem_m(10|14|15|27|47|52|57|58|62|63|75|78|81|82|84|85|90|92|102|107|119)_test|mnemos_manifests_irem_redalert_test|mnemos_manifests_irem_m10_system_test|mnemos_manifests_irem_m14_system_test|mnemos_manifests_irem_m27_system_test|mnemos_manifests_irem_m47_system_test|mnemos_manifests_irem_m57_system_test|mnemos_manifests_irem_m62_system_test|mnemos_manifests_irem_m63_system_test|mnemos_manifests_irem_m102_system_test|mnemos_manifests_irem_m119_system_test|mnemos_manifests_irem_travrusa_test|irem_m10_.*golden|irem_m14_.*golden|irem_m15_.*golden|irem_m27_.*golden|irem_m47_.*golden|irem_m52_.*golden|irem_m57_.*golden|irem_m58_.*golden|irem_m62_.*golden|irem_m63_.*golden|irem_m72_.*golden|irem_m75_.*golden|irem_m78_.*golden|irem_m81_.*golden|irem_m82_.*golden|irem_m84_.*golden|irem_m85_.*golden|irem_m90_.*golden|irem_m92_.*golden|irem_m102_.*golden|irem_m107_.*golden|irem_m119_.*golden|irem_redalert_.*golden|irem_travrusa_.*golden"
$ctestExit = $LASTEXITCODE
if ($ctestExit -ne 0) {
    exit $ctestExit
}

$runnerExit = 0

$m72Runner = Join-Path $PSScriptRoot "irem_m72/run-corpus-smoke.ps1"
if (Test-Path $m72Runner) {
    Write-Host "`nRunning Irem M72 corpus smoke ..." -ForegroundColor Cyan
    & $m72Runner -BuildDir $BuildDir -Recurse
    $m72Exit = $LASTEXITCODE
    if ($m72Exit -ne 0 -and $runnerExit -eq 0) {
        $runnerExit = $m72Exit
    }
}

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
    if ($inventoryExit -ne 0 -and $runnerExit -eq 0) {
        $runnerExit = $inventoryExit
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
    if ($gnetExit -ne 0 -and $runnerExit -eq 0) {
        $runnerExit = $gnetExit
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
    $taitoF2Exit = $LASTEXITCODE
    if ($taitoF2Exit -ne 0 -and $runnerExit -eq 0) {
        $runnerExit = $taitoF2Exit
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
