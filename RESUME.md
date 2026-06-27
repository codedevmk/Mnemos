# MSX / MSX2 Resume Handoff

Generated: 2026-06-26 23:03 America/Chicago
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote: `origin/feature/msx2`

This handoff exists because the original Codex session ran for roughly 30
hours and hit practical context-window limits. Continue from this file instead
of reconstructing the full chat history.

## Start Here

Use the MSX2 worktree, not the root checkout:

```powershell
Set-Location C:\dev\emu\Mnemos-msx2
git fetch origin
git status --short --branch --untracked-files=all
git log -8 --oneline --decorate
Get-Content .\CONSTITUTION.md
Get-Content .\README.md
Get-Content .\RESUME.md
```

The root checkout at `C:\dev\emu\Mnemos` was on an unrelated branch during the
handoff work. Continue MSX/MSX2 work from `C:\dev\emu\Mnemos-msx2`.

## User Contract

- Implement both MSX and MSX2; they share common manifest, adapter, player
  launch, mapper, video, and golden-test surfaces.
- Do not claim "100% working" until representative real-ROM/media validation
  proves it. Current state is partial.
- A blank Mnemos Player window is not proof. Launch with explicit `--system`
  and `--rom`, or use the data-gated smoke runner.
- Preserve the requested branch/worktree name: `feature/msx2`.
- Keep ROMs, firmware, screenshots, logs, and build outputs out of Git.

## BIOS / ROM Inputs

The user's C-BIOS root is:

```text
D:\emu\msx\bios
```

Known firmware files used by this worktree:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_main_msx2_br.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
```

The local cartridge corpus used in this session is:

```text
D:\emu\msx\MSX files [ROM]
```

All runtime artifacts should stay under `build\`, preferably
`build\scratch\...`.

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- MSX and MSX2 C-BIOS boot paths run through golden tests and explicit player
  launches when invoked with system/media arguments.
- MSX and MSX2 share RAM-size profile semantics through the manifest, player
  launch, adapter, smoke script, and golden-test paths.
- `3D Pool [cas2rom64ks]` now auto-detects as ASCII16 through the shared mapper
  resolver and boots through both MSX and MSX2.
- `a_test2 [Arabic MSX].rom` is profiled for MSX2 BR C-BIOS instead of being
  treated as a V9938 renderer failure.
- Bounded real-ROM smoke windows have passed through skip 59:
  - `-SkipRoms 12 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 24 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 36 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 48 -MaxRoms 12`: `26/26` passed after profiling
    `a_test2 [Arabic MSX].rom` against BR C-BIOS.

Known gaps:

- The latest bounded corpus window at `-SkipRoms 60 -MaxRoms 12` exposed one
  current failure: MSX `abbaye_v1.1.rom` stays at the firmware framebuffer
  baseline while the MSX2 case renders distinct data.
- This is not yet a representative compatibility matrix.
- Earlier notes included Bosconia staying on the C-BIOS logo and MSX2 Bestial
  Warrior color fidelity suspicion; those still need confirmation in later
  slices.

## Latest Passing Commit Before Handoff

The branch was clean and pushed at:

```text
6ded9c47 (HEAD -> feature/msx2, origin/feature/msx2) Profile Arabic MSX2 regional BIOS boot
```

This handoff update should be committed and pushed after writing this file.

## Latest Validation: Skip 60 Window

Command run:

```powershell
$romDir='D:\emu\msx\MSX files [ROM]'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -MsxBios 'D:\emu\msx\bios\cbios\cbios_main_msx1.rom' `
  -MsxLogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx1.rom' `
  -MsxRomDir $romDir `
  -MsxRegion 'ntsc' `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2RomDir $romDir `
  -Msx2ExpandedSlots '8' `
  -Msx2SubSlot '3.0' `
  -Msx2RamSlot '3.2' `
  -Msx2RamSize '512K' `
  -Msx2Region 'ntsc' `
  -RomProfileManifest 'tests/golden/msx_rom_profiles.json' `
  -Frames 600 `
  -RetryFrames 3600 `
  -SkipRoms 60 `
  -MaxRoms 12 `
  -RequireData
```

Result:

```text
MSX/MSX2 boot smoke: 25/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-225824-741-71812\summary.json
```

Only failure:

```text
[fail] msx/rom-abbaye_v1.1 exit=0 reason=framebuffer matched firmware baseline; media did not visibly affect boot
log:      build\scratch\msx-boot\20260626-225824-741-71812\006-msx-rom-abbaye_v1.1.log
retryLog: build\scratch\msx-boot\20260626-225824-741-71812\006-msx-rom-abbaye_v1.1-retry-3600.log
```

Important hashes:

```text
MSX firmware baseline:
697fe93903980d26d6ef37fc76b0511c8a76656ebdc70f7be616c4dd7bac836a

MSX abbaye_v1.1 after 600/3600 frames:
697fe93903980d26d6ef37fc76b0511c8a76656ebdc70f7be616c4dd7bac836a

MSX2 firmware baseline:
8572e9f4e74913c1a5d3de86da650c0a46ab5ec0643b2d6e5f80bf0c3fd5e1bb

MSX2 abbaye_v1.1:
9ad46a5888798bf12b44e407a91ac4e75e5087ccc2439a6427cae495a039bf9c
```

## Current Failure Details

ROM:

```text
D:\emu\msx\MSX files [ROM]\abbaye_v1.1.rom
Size: 262144
SHA256: A8F52BE42A9731DF93D68B78F1BA00F03BF2B04582CA532359B27AD8D3EF25B9
```

Observed:

- MSX run exits successfully from Catch2 but the smoke runner rejects it because
  the framebuffer hash equals the MSX C-BIOS firmware baseline.
- Retry to 3600 frames still equals the same baseline hash.
- MSX2 run for the same ROM is visibly affected and produces hash
  `9ad46a5888798bf12b44e407a91ac4e75e5087ccc2439a6427cae495a039bf9c`.
- The ROM header starts with `41 42 AE 40`, giving an init vector around
  `$40AE`.
- Early bytes include writes to mapper-looking ranges such as `$7000` and
  `$7800`, so confirm mapper selection before adding any skip profile.
- `rg` found no existing profile for `abbaye` or its ROM hash.

## Resume Triage Steps

First rerun the failing target with Catch2 success output enabled; the smoke
runner suppresses INFO on successful Catch2 assertions:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_ROM='D:\emu\msx\MSX files [ROM]\abbaye_v1.1.rom'
$env:MNEMOS_MSX_REGION='ntsc'
$env:MNEMOS_MSX_BOOT_FRAMES='3600'
build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe "[golden][msx]" -s
```

Then try explicit mapper overrides before deciding this is a profile skip:

```powershell
$rom='D:\emu\msx\MSX files [ROM]\abbaye_v1.1.rom'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -MsxBios 'D:\emu\msx\bios\cbios\cbios_main_msx1.rom' `
  -MsxLogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx1.rom' `
  -MsxRom $rom `
  -MsxMapper 'ascii16' `
  -MsxRegion 'ntsc' `
  -Frames 3600 `
  -RequireData
```

If ASCII16 does not boot, also check the mapper names already accepted by the
codebase for this surface, especially `ascii8` and `generic8`. Inspect
`src\manifests\common\msx_cartridge_mapper.cpp`,
`src\manifests\common\tests\msx_cartridge_mapper_test.cpp`, and
`tests\golden\msx_boot_test.cpp` before editing.

Decision points:

- If a mapper override boots MSX, add a narrow SHA256-backed ROM profile or
  mapper detector exception plus tests.
- If MSX never boots but MSX2 does, add an MSX-only skip profile with a clear
  reason that the cartridge is validated through MSX2/C-BIOS and is not an MSX1
  compatibility datapoint.
- If direct diagnostics show slot or BIOS handoff failure unrelated to mapper
  metadata, implement the narrow shared slot/boot fix and validate both MSX and
  MSX2.

## Validation After The Next Fix

At minimum, rerun a targeted `abbaye_v1.1` smoke for both systems or the
relevant skip/profile path. Then rerun the full failing corpus window:

```powershell
$romDir='D:\emu\msx\MSX files [ROM]'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -MsxBios 'D:\emu\msx\bios\cbios\cbios_main_msx1.rom' `
  -MsxLogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx1.rom' `
  -MsxRomDir $romDir `
  -MsxRegion 'ntsc' `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2RomDir $romDir `
  -Msx2ExpandedSlots '8' `
  -Msx2SubSlot '3.0' `
  -Msx2RamSlot '3.2' `
  -Msx2RamSize '512K' `
  -Msx2Region 'ntsc' `
  -RomProfileManifest 'tests/golden/msx_rom_profiles.json' `
  -Frames 600 `
  -RetryFrames 3600 `
  -SkipRoms 60 `
  -MaxRoms 12 `
  -RequireData
```

If the window passes, continue with:

```text
-SkipRoms 72 -MaxRoms 12
```

Use explicit player proof only for real render/boot fixes. If the next change is
only a skip profile for an MSX2-only cartridge, document why player proof was
not run.

## Build / Test Commands

Windows MSVC build/test must run from a Visual Studio developer environment.
This command form has worked reliably from PowerShell:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test'
```

Focused tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test|mnemos_manifests_common_test" --output-on-failure'
```

If Ninja/MSVC hits a transient `LNK1104` archive race, retry the build with:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --parallel 1'
```

## Validation Already Completed

Focused RAM-size/profile build:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_apps_player_system_launch_test mnemos_msx_boot_test mnemos_apps_player_msx_adapter_test mnemos_manifests_msx_test'
```

Result:

```text
passed
```

Focused RAM-size/profile tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_apps_player_system_launch_test|mnemos_msx_boot_test|mnemos_apps_player_msx_adapter_test|mnemos_manifests_msx_test" --output-on-failure'
```

Result:

```text
100% tests passed, 0 tests failed out of 4
```

Mapper-focused build and tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_common_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_common_test|mnemos_msx_boot_test" --output-on-failure'
```

Result:

```text
100% tests passed, 0 tests failed out of 2
```

Real-data `3D Pool [cas2rom64ks]` MSX/MSX2 smoke:

```text
MSX/MSX2 boot smoke: 4/4 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-221758-220-55992\summary.json
MSX ROM hash: 28e81fcc84603a60d9c5fe8489a9292fb383bf85ce304f0622fe1906bb35894a
MSX2 ROM hash: 39285647f88ac0fac02e87cc039f1e113246f52079eeea1da5c72f0afe874b0f
```

Explicit player proof for `3D Pool [cas2rom64ks]`:

```text
MSX screenshot: build\scratch\msx-cas2rom-proof\20260626-221758\3d-pool-cas2rom-msx.ppm
MSX screenshot SHA256: 73C06DC60666EAADDDBFD2F2A5A28FBA33001CB8C9C9D6562D4DF5FA1A10B8AF
MSX2 screenshot: build\scratch\msx-cas2rom-proof\20260626-221758\3d-pool-cas2rom-msx2.ppm
MSX2 screenshot SHA256: 4977EC40C4D19E041EDB1282C6083ADADEF1EB05A0E2A285DF3FEB9BFE1B0893
```

Arabic MSX2 BR C-BIOS proof:

```text
MSX/MSX2 boot smoke: 2/2 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224621-597-43940\summary.json
rom hash: 7855330785cbb9bcd8ba95149af8eef1fa67f035abbdc88c61e38fc0b6d27d9d
```

Profile-driven Arabic MSX2 proof:

```text
MSX/MSX2 boot smoke: 2/2 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224755-972-58776\summary.json
```

Explicit player proof for Arabic MSX2:

```text
mnemos_player wrote build\scratch\msx2-a-test2-proof\20260626-225405\a-test2-msx2-br.ppm
PPM SHA256: B4DD3D7053F38E6729A808858CC703CEFC063E856A4AE1CE89738F5E95CAAC19
PPM non_uniform=True
```

Latest passing corpus windows:

```text
-SkipRoms 24 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-222804-481-14904\summary.json

-SkipRoms 36 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-223029-317-62816\summary.json

-SkipRoms 48 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224915-455-69380\summary.json
```

## Handoff Notes

- Use `rg` first for searches and batch reads where possible.
- Use `apply_patch` for manual source edits.
- Do not vendor emulator source or ROM data.
- Do not treat a successful Catch2 process exit as proof when the smoke runner
  reports a firmware-baseline framebuffer.
- Preserve both MSX and MSX2 behavior in every shared fix.
- If the next framebuffer issue is fixed, continue bounded corpus validation
  rather than claiming broad compatibility from one ROM.
