#!/usr/bin/env pwsh
# Run the data-gated tests against a local ROM / test-corpus library.
#
# Several tests need copyrighted material that is NEVER committed (SMS cartridges,
# C64 BASIC/KERNAL/CHARGEN ROMs, the Z80 ZEXALL exerciser, the SingleStepTests 6502
# vectors). Each one self-SKIPs unless an environment variable points at the data:
#
#   MNEMOS_SMS_ROM            a .sms cartridge image                  -> sms_boot_test
#   MNEMOS_C64_ROM_DIR        dir with BASIC/KERNAL/CHARGEN images    -> c64_basic_boot_test
#   MNEMOS_Z80_ZEX_ROM        zexall.com / zexdoc.com                 -> z80_zexall_test
#   MNEMOS_M6510_TOMHARTE_DIR SingleStepTests 6502 JSON directory     -> m6510_tomharte_test
#
# Optional golden-hash pins (assert the rendered framebuffer once locked in):
#   MNEMOS_SMS_BOOT_SHA256, MNEMOS_C64_BOOT_SHA256
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
    @{ Name = "MNEMOS_SMS_ROM";            Test = "sms_boot_test" },
    @{ Name = "MNEMOS_C64_ROM_DIR";        Test = "c64_basic_boot_test" },
    @{ Name = "MNEMOS_Z80_ZEX_ROM";        Test = "z80_zexall_test" },
    @{ Name = "MNEMOS_M6510_TOMHARTE_DIR"; Test = "m6510_tomharte_test" }
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
ctest --test-dir $testDir --output-on-failure -R "tomharte|zexall|c64_basic_boot|sms_boot"
exit $LASTEXITCODE
