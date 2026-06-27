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
- Bounded real-ROM smoke windows passed through skip 47:
  - `-SkipRoms 12 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 24 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 36 -MaxRoms 12`: `26/26` passed.
- The next window, `-SkipRoms 48 -MaxRoms 12`, found one MSX2-only failure:
  `a_test2 [Arabic MSX].rom`.

Known gaps:

- This is not yet a representative compatibility matrix.
- The latest active failure appears to be V9938 framebuffer/render plumbing for
  MSX2 Graphics I / 256-pixel output, not cartridge mapping.
- Earlier notes included Bosconia staying on the C-BIOS logo and MSX2 Bestial
  Warrior color fidelity suspicion; those still need confirmation after the
  current V9938 issue is fixed.

## Latest Failure To Resume

Run this first if continuing implementation:

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
  -Frames 600 `
  -RetryFrames 3600 `
  -RequireData
```

Most recent corpus command that exposed it:

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
MSX/MSX2 boot smoke: 25/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-223424-882-3140\summary.json
failed case: msx2/rom-a_test2 [Arabic MSX]
```

Failure details:

```text
ROM: D:\emu\msx\MSX files [ROM]\a_test2 [Arabic MSX].rom
Size: 32768 bytes
SHA256: A4D5EB1097D36B0C170603577F2A99546E01CC88171AEE649E58C7882182CCED
Mapper: Plain
CPU PC: $C93B
Current opcode byte: $33
VDP mode: 0 / Graphics I
VDP registers: r0=$00 r1=$A0 r2=$06 r7=$01
VRAM nonzero bytes: 2399
V9938 visible_g4_nonzero: 2303
V9938 visible_g1_hist: [0,48107,0,0,0,185,0,0,0,0,0,0,0,0,0,860]
First visible G1 non-backdrop: line=41 x=114 pen=$05 resolved=$05 pattern=$00
Framebuffer check: still reported uniform
Framebuffer hash: b08b67f5c82a88b2b691de151eab65f4c7a06c9a14bef0abe94a53b193fd8a9f
```

The diagnostic data shows non-background Graphics I pixels in VRAM-derived
render analysis while the exposed framebuffer remains uniform. Treat this as a
V9938 render-to-framebuffer or viewport/copy issue until proven otherwise.

## Files To Inspect Next

Start with these files:

```text
src/chips/video/v9938/v9938.cpp
src/chips/video/v9938/v9938.hpp
src/chips/video/v9938/tests/v9938_test.cpp
tests/golden/msx_boot_test.cpp
```

Relevant observations already made:

- `v9938::mode()` maps `r0=$00`, `r1=$A0` to `display_mode::graphics_i`.
- `visible_width()` returns 256 for Graphics I.
- `render_unadjusted_scanline()` calls `render_graphics_i_scanline()` and
  `render_sprites_mode1()` for Graphics I.
- Existing V9938 Graphics I tests pass, but they likely do not exercise the
  failing 256-width framebuffer/viewport path observed by `a_test2`.
- The TMS9918A Graphics I path is useful as a reference because MSX1 passes
  the same ROM.

## Suggested Next Implementation Slice

1. Add a focused V9938 regression that reproduces Graphics I with the failure
   register shape, especially `r1=$A0`, `r2=$06`, and a nonzero name/color/pattern
   entry that should affect a visible pixel around line 41, x 114.
2. Inspect `render_scanline()`, `render_unadjusted_scanline()`, framebuffer
   dimensions, and any 256-to-512 width copy/centering logic.
3. Fix the renderer so the public framebuffer reflects Graphics I pixels in
   MSX2/V9938 mode.
4. Re-run the focused V9938 tests and the targeted `a_test2` MSX2 smoke.
5. Re-run the `-SkipRoms 48 -MaxRoms 12` corpus window and then continue to
   the next bounded window.
6. Take explicit player proof with `--system msx2 --rom ... --screenshot ...`;
   do not use a blank player startup as proof.

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
```

Latest failing corpus window:

```text
-SkipRoms 48 -MaxRoms 12:
MSX/MSX2 boot smoke: 25/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-223424-882-3140\summary.json
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
