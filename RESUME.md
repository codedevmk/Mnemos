# MSX / MSX2 Resume Handoff

Generated: 2026-06-27
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote: `origin/feature/msx2`

This handoff exists because the original Codex session had roughly 30 hours of
history and was approaching context-window limits.

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

The root checkout at `C:\dev\emu\Mnemos` was clean during handoff but was on an
unrelated branch, `codex/implement-amiga500-emulation`. Continue MSX/MSX2 work
from `C:\dev\emu\Mnemos-msx2`.

## User Contract

- Implement both MSX and MSX2; they share common manifest, adapter, player
  launch, mapper, video, and golden-test surfaces.
- Do not claim "100% working" until representative real-ROM/media validation
  proves it. Current state is partial.
- A blank Mnemos Player window is not proof. Launch with explicit `--system`
  and `--rom`, or use the data-gated smoke runner.
- Preserve the requested branch/worktree name: `feature/msx2`.

## BIOS / ROM Inputs

The user's C-BIOS root is:

```text
D:\emu\msx\bios
```

Known firmware files:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
```

The local cartridge corpus used in this session is:

```text
D:\emu\msx\MSX files [ROM]
```

Do not commit firmware, ROMs, screenshots, logs, or build outputs. Keep all
runtime artifacts under `build\`.

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- MSX and MSX2 C-BIOS boot paths run through golden tests and player launch
  when invoked with explicit system/media.
- MSX and MSX2 share RAM-size profile semantics through the manifest, player
  launch, adapter, smoke script, and golden-test paths.
- `3D Pool [cas2rom64ks]` now auto-detects as ASCII16 through the shared mapper
  resolver and boots through both MSX and MSX2.
- Bounded real-ROM smoke windows passed through skip 59:
  - `-SkipRoms 12 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 24 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 36 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 48 -MaxRoms 12`: `26/26` passed after profiling
    `a_test2 [Arabic MSX].rom` against the BR C-BIOS MSX2 main ROM.

Known gaps:

- This is not yet a representative compatibility matrix.
- Continue the bounded corpus at `-SkipRoms 60 -MaxRoms 12`.
- Earlier notes included Bosconia staying on the C-BIOS logo and MSX2 Bestial
  Warrior color fidelity suspicion; those still need confirmation in later
  slices.

## Latest Change

The latest change adds a ROM profile for:

```text
D:\emu\msx\MSX files [ROM]\a_test2 [Arabic MSX].rom
SHA256: A4D5EB1097D36B0C170603577F2A99546E01CC88171AEE649E58C7882182CCED
```

The earlier failure with generic MSX2 C-BIOS was not a V9938 renderer bug. The
VDP state had `r1=$A0`, so display-enable bit 6 was clear and a uniform
framebuffer was expected. The same cartridge boots visibly on MSX2 with
`cbios_main_msx2_br.rom`, matching the existing note in `tests/golden/README.md`
that some Arabic MSX2 cartridges need regional C-BIOS metadata.

Profile added in:

```text
tests/golden/msx_rom_profiles.json
```

Profile behavior:

```text
system: msx2
mapper: plain
bios: cbios_main_msx2_br.rom
boot_frames: 3600
boot_sha256: 7855330785cbb9bcd8ba95149af8eef1fa67f035abbdc88c61e38fc0b6d27d9d
```

## Latest Validation

Direct BR C-BIOS proof for `a_test2 [Arabic MSX].rom`:

```powershell
$rom='D:\emu\msx\MSX files [ROM]\a_test2 [Arabic MSX].rom'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2_br.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2Rom $rom `
  -Msx2ExpandedSlots '8' `
  -Msx2SubSlot '3.0' `
  -Msx2RamSlot '3.2' `
  -Msx2RamSize '512K' `
  -Msx2Region 'ntsc' `
  -Frames 3600 `
  -RequireData
```

Result:

```text
MSX/MSX2 boot smoke: 2/2 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224621-597-43940\summary.json
rom hash: 7855330785cbb9bcd8ba95149af8eef1fa67f035abbdc88c61e38fc0b6d27d9d
```

Profile-driven targeted proof using the generic base C-BIOS command:

```powershell
$rom='D:\emu\msx\MSX files [ROM]\a_test2 [Arabic MSX].rom'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2Rom $rom `
  -Msx2ExpandedSlots '8' `
  -Msx2SubSlot '3.0' `
  -Msx2RamSlot '3.2' `
  -Msx2RamSize '512K' `
  -Msx2Region 'ntsc' `
  -RomProfileManifest 'tests/golden/msx_rom_profiles.json' `
  -Frames 600 `
  -RetryFrames 3600 `
  -RequireData
```

Result:

```text
MSX/MSX2 boot smoke: 2/2 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224755-972-58776\summary.json
```

The summary records `hash=null` for this profile-backed case because the golden
hash is asserted inside Catch2 and Catch2 does not print the computed hash on a
passing assertion.

Original failing corpus window after the profile fix:

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
  -SkipRoms 48 `
  -MaxRoms 12 `
  -RequireData
```

Note: if copying commands from chat history, verify all C-BIOS paths before
running. Older messages may contain a stale `-MsxLogoRom` path.

Result:

```text
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224915-455-69380\summary.json
```

Explicit player proof for the same ROM:

```powershell
$proofDir='build\scratch\msx2-a-test2-proof\20260626-225405'
New-Item -ItemType Directory -Force -Path $proofDir | Out-Null
$rom='D:\emu\msx\MSX files [ROM]\a_test2 [Arabic MSX].rom'
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2_br.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_MSX2_REGION='ntsc'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe `
  --system msx2 `
  --rom $rom `
  --screenshot "$proofDir\a-test2-msx2-br.ppm" `
  --frames 3600
```

Result:

```text
mnemos_player wrote build\scratch\msx2-a-test2-proof\20260626-225405\a-test2-msx2-br.ppm
PPM SHA256: B4DD3D7053F38E6729A808858CC703CEFC063E856A4AE1CE89738F5E95CAAC19
PPM non_uniform=True
```

## Suggested Next Implementation Slice

Run the next bounded MSX/MSX2 ROM corpus smoke:

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

Use the next failing case from that window to choose the next compatibility
slice. Avoid broad renderer, slot, or mapper changes when a ROM profile captures
a known firmware/regional dependency.

## Build / Test Commands

Windows MSVC build/test must run from a Visual Studio developer environment.
This command form has worked reliably from PowerShell:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test'
```

Focused tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

If Ninja/MSVC hits a transient `LNK1104` archive race, retry the build with:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --parallel 1'
```

## Validation Already Completed

Focused build for the RAM-size/profile slice:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_apps_player_system_launch_test mnemos_msx_boot_test mnemos_apps_player_msx_adapter_test mnemos_manifests_msx_test'
```

Result:

```text
passed
```

Focused tests:

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

Explicit player proof for the same ROM:

```text
MSX screenshot: build\scratch\msx-cas2rom-proof\20260626-221758\3d-pool-cas2rom-msx.ppm
MSX screenshot SHA256: 73C06DC60666EAADDDBFD2F2A5A28FBA33001CB8C9C9D6562D4DF5FA1A10B8AF
MSX2 screenshot: build\scratch\msx-cas2rom-proof\20260626-221758\3d-pool-cas2rom-msx2.ppm
MSX2 screenshot SHA256: 4977EC40C4D19E041EDB1282C6083ADADEF1EB05A0E2A285DF3FEB9BFE1B0893
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

## Recent Commits

Recent branch history before this handoff commit:

```text
0d15a025 Refresh MSX2 resume handoff
1e9dec08 Fix 3D Pool MSX mapper detection
9e4899d7 Refresh MSX2 resume handoff
7874e359 Share MSX RAM size profile path
3d0cbd5d Profile Arabic MSX boot proof
f97a65fc Expose V9938 palette sidecar
54c02b79 Refresh MSX2 resume handoff
fdede017 Add MSX2 lower plain ROM handoff
```

After pulling, the next agent should see a newer commit that updates this file.

## Handoff Notes

- Use `rg` first for searches and batch reads where possible.
- Keep build and smoke artifacts under `build\scratch\`.
- Use `apply_patch` for manual source edits.
- Do not vendor emulator source or ROM data.
- Preserve both MSX and MSX2 behavior in every shared fix.
- If the framebuffer issue is fixed, continue bounded corpus validation rather
  than claiming broad compatibility from one ROM.
