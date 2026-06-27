# MSX / MSX2 Handoff

Generated: 2026-06-27 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote branch: `origin/feature/msx2`
Parent checkpoint before this handoff refresh: `3a30372c Update MSX2 handoff diagnostics`

This is the resume point for the MSX/MSX2 implementation work. The original
Codex session ran for roughly 30 hours and hit practical context-window limits.
Continue from this file and the live worktree state instead of reconstructing
the chat history.

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

The root checkout at `C:\dev\emu\Mnemos` is not the active MSX/MSX2 worktree.

Windows validation should run under Visual Studio DevCmd:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

## User Contract

- Implement both MSX and MSX2; they share common manifests, mapper code, player
  adapters, VDP behavior, smoke scripts, and golden-test surfaces.
- Preserve the requested worktree and branch: `feature/msx2`.
- C-BIOS lives under `D:\emu\msx\bios`.
- The ROM corpus used for this slice is `D:\emu\msx\MSX files [ROM]`.
- Do not claim "100% working" until real ROM/media validation proves it.
- A blank Mnemos Player window is not proof. Launch with explicit `--system`
  and `--rom`, or use the data-gated smoke runner.
- Do not commit ROMs, firmware, screenshots, logs, or build outputs.
- Keep transient diagnostics under `build\scratch\...`.

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- C-BIOS MSX and MSX2 boot paths are wired through golden tests and explicit
  player launch paths.
- Shared RAM-size profile semantics, cartridge mapper resolution, smoke-script
  routing, and MSX/MSX2 golden-test hooks are in place.
- Earlier bounded real-ROM smoke windows passed through skip 191.
- Focused V9938 and MSX boot tests pass at the last validated checkpoint.

Active blocker:

- Skip-192 still fails on MSX2 for `bean.rom`.
- MSX `bean.rom` passes in the same smoke window.
- MSX2 `bean.rom` exits with code 42 because the framebuffer remains uniform
  after the 3600-frame retry.
- Do not mark the goal complete until this and broader real-ROM coverage pass.

## Latest Diagnosis

The previous handoff focused on `$C000` staging and the bad `$BFFF/$C000`
execution path. The latest forced diagnostics narrowed the likely root cause:
MSX2 `bean.rom` reaches a halted CPU state after executing the staged `$C000`
payload, and the final V9938 state has nonzero visible Graphics 4 VRAM, but the
sampled framebuffer is still uniform.

That points more strongly at V9938 bitmap rendering, source page selection,
display-enable timing, or framebuffer update cadence than at C-BIOS region,
expanded-slot profile, or the plain ROM mapper.

Most important parsed MSX2 state from
`build\scratch\msx-bean-diagnostics\variant-generic-3.2.log`:

```text
resolved cartridge mapper: Plain
cpu pc/sp/af/bc/de/hl: $CA3E/$E6FF/$5FBA/$0101/$5F01/$66B8 halted=true iff1=false iff2=false im=0 cycles=215049602
slot state: primary=$D0 ... secondary3=$A0
ram mapper segments: [3,2,1,0]
pc window: [$CA36=$33,$CA37=$33,$CA38=$33,$CA39=$33,$CA3A=$33,$CA3B=$35,$CA3C=$77,$CA3D=$76,$CA3E=$33,...]
vdp state: frame=3600 mode=4 r0=$02 r1=$E2 r2=$06 r7=$F4 r15=$00 s0=$C4 s1=$01 irq=true vram_nonzero=6104 first_pixel=2368548
v9938 extended state: r8=$08 r9=$00 r3=$FF r4=$03 r5=$36 r6=$07 r10=$00 r11=$00 r18=$00 r13=$00 r23=$00 r44=$00 r45=$00 r46=$08 s2=$0C vram_pages_32k=[6103,0,1,0] visible_g4_nonzero=6008 visible_g4_hist=[42176,3820,0,376,0,0,...]
```

Interpretation:

- The CPU is not crashing immediately at reset; it runs the ROM path and halts
  at `$CA3E`.
- Page 3 remains RAM and the RAM mapper segments are stable.
- V9938 reports Graphics 4 mode with nonzero visible Graphics 4 VRAM on page 0.
- `first_pixel=2368548` is nonzero backdrop-like output, but the framebuffer
  uniformity check still fails.
- The next slice should inspect how/when `v9938::framebuffer()` is populated
  relative to `v9938::tick(...)`, scanline rendering, and `render_frame()`.

## Diagnostics Already Run

Forced MSX1 diagnostic:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_ROM='D:\emu\msx\MSX files [ROM]\bean.rom'
$env:MNEMOS_MSX_BOOT_FRAMES='600'
$env:MNEMOS_MSX_BOOT_SHA256='force-diagnostics'
$env:MNEMOS_MSX_MEM_WATCH='$C000-$C0FF'
$env:MNEMOS_MSX_MEM_WATCH_PC='$8000-$8400'
$env:MNEMOS_MSX_PC_WATCH='$BFF0-$C080'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx]'
```

Log:

```text
build\scratch\msx-bean-diagnostics\resume-msx-memwatch-c000-c0ff-pc8000-8400.log
```

Result: expected forced-hash exit 42; no `$C000` memory-watch or PC-watch events
in that filtered window.

Forced MSX2 diagnostic:

```powershell
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_ROM='D:\emu\msx\MSX files [ROM]\bean.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_MSX2_REGION='ntsc'
$env:MNEMOS_MSX2_BOOT_FRAMES='600'
$env:MNEMOS_MSX2_BOOT_SHA256='force-diagnostics'
$env:MNEMOS_MSX_MEM_WATCH='$C000-$C0FF'
$env:MNEMOS_MSX_MEM_WATCH_PC='$8000-$8400'
$env:MNEMOS_MSX_PC_WATCH='$BFF0-$C080'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx2]'
```

Log:

```text
build\scratch\msx-bean-diagnostics\resume-msx2-memwatch-c000-c0ff-pc8000-8400.log
```

Result: expected forced-hash exit 42.

C-BIOS / slot / RAM variants tested at 3600 frames:

```text
variant-generic-3.2.log
variant-eu-3.2.log
variant-jp-3.2.log
variant-br-3.2.log
variant-generic-3.0.log
variant-generic-3.1.log
```

All variants fail with the same uniform-framebuffer symptom. This is not a
simple C-BIOS region, subslot, RAM slot, or RAM size-profile issue.

## Existing Checkpoint Contents

The branch already contains these relevant changes:

- `tests/golden/msx_boot_test.cpp`: opt-in VDP I/O diagnostics with V9938 frame
  index, decoded display mode, and registers `r5`, `r6`, `r8`, `r9`, `r11`,
  and `r23`.
- `tests/golden/msx_boot_test.cpp`: opt-in memory-write diagnostics via
  `MNEMOS_MSX_MEM_WATCH` and optional PC filtering through
  `MNEMOS_MSX_MEM_WATCH_PC`.
- `src/chips/video/v9938/v9938.cpp`: S#0 reads now clear only the frame
  interrupt bit and preserve sprite overflow, collision, and low sprite index
  state.
- `src/chips/video/v9938/tests/v9938_test.cpp`: focused V9938 S#0 regressions
  for preserved sprite overflow and collision state.

Rationale for the V9938 S#0 change:

- Yamaha V9938 documentation describes S#0 bit F as reset by reading S#0.
- It does not describe S#0 sprite overflow/collision bits as reset by the read.
- The regression is correct and test-backed, but it did not fix `bean.rom`.

Reference used during diagnosis:

```text
https://archive.org/stream/bitsavers_yamahaYamanicalDataBookAug85_6932685/Yamaha_V9938_MSX-Video_Technical_Data_Book_Aug85_djvu.txt
```

## Known Smoke Command

Skip-192 smoke command:

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
  -SkipRoms 192 `
  -MaxRoms 12 `
  -RequireData
```

Last known result:

```text
MSX/MSX2 boot smoke: 25/26 passed
failure: msx2/rom-bean exit=42
```

Do not treat a player boot without explicit `--system` and `--rom` as evidence.
The blank-player screenshot from the user came from launching the player without
the target system or ROM arguments.

## New Diagnostic Knobs

`tests/golden/msx_boot_test.cpp` supports:

```text
MNEMOS_MSX_MEM_WATCH     trace memory writes in a range, or all writes with 1/true/all
MNEMOS_MSX_MEM_WATCH_PC  optional PC range filter for memory-write trace
```

Example:

```powershell
$env:MNEMOS_MSX_MEM_WATCH='$C000-$C080'
$env:MNEMOS_MSX_MEM_WATCH_PC='$8000-$C100'
$env:MNEMOS_MSX_PC_WATCH='$BFF0-$C080'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx2]'
```

The watcher logs write PC, cycle count, address/value, Z80 registers, page 3
slot and RAM mapper state, a byte window around PC, and a byte window around
the write address. It is opt-in and clears the bus observer after each boot run.

Known caveat:

- Large watch ranges can produce huge Catch2 `UNSCOPED_INFO` lines. If further
  diagnostics need wider capture, first summarize the output or cap by address
  subrange.

## Relevant Source Surfaces

- `src/chips/video/v9938/v9938.cpp`
- `src/chips/video/v9938/v9938.hpp`
- `src/chips/video/v9938/tests/v9938_test.cpp`
- `src/chips/video/tms9918a/tms9918a.cpp`
- `tests/golden/msx_boot_test.cpp`
- `scripts/msx/run-boot-smoke.ps1`
- `tests/golden/msx_rom_profiles.json`
- `src/manifests/common/msx_cartridge_mapper.cpp`
- `src/manifests/msx/msx_system.cpp`
- `src/manifests/msx2/msx2_system.cpp`
- `src/apps/player/adapters/msx2/`

Useful immediate inspection commands:

```powershell
rg -n "void v9938::tick|finish_scanline|render_frame\(|framebuffer\(" src/chips/video/v9938 src/apps src/runtime src/manifests tests -g "*.cpp" -g "*.hpp"
Get-Content src\chips\video\v9938\v9938.cpp | Select-Object -Skip 330 -First 120
Get-Content src\chips\video\v9938\v9938.cpp | Select-Object -Skip 2080 -First 80
Get-Content tests\golden\msx_boot_test.cpp | Select-Object -Skip 1880 -First 140
Select-String -Path 'build\scratch\msx-bean-diagnostics\variant-generic-3.2.log' -Pattern 'palette=' -Context 0,0
```

## Suggested Next Slice

1. Inspect V9938 frame/scanline rendering cadence and every call site of
   `render_frame()` and `framebuffer()`.
2. Determine whether `bean.rom` writes visible Graphics 4 VRAM and enables
   display after the last rendered scanline, leaving the framebuffer stale at
   the test sample point.
3. As a local diagnostic only, verify whether an explicit final
   `sys->vdp.render_frame()` before the framebuffer uniformity check makes the
   MSX2 `bean.rom` frame nonuniform.
4. If final rendering fixes the symptom, implement the semantically correct
   runtime/test/player path so presented frames reflect the current VDP state.
5. If final rendering stays uniform, inspect Graphics 4 source base, palette
   entries, color-0 transparency, display-enable state, and register-derived
   page selection.
6. Add a focused regression before changing behavior, then rerun the skip-192
   smoke window and broader MSX/MSX2 corpus coverage.

## Completion Bar

Do not mark the MSX/MSX2 goal complete until:

- `bean.rom` passes both MSX and MSX2 in the skip-192 smoke window.
- The fix has focused regression coverage in chip/system/golden tests as
  appropriate.
- Explicit player launches use `--system` and `--rom` and render non-blank,
  game-specific output.
- A broader MSX/MSX2 corpus window passes after the skip-192 fix.
- The Windows MSVC preset build and relevant tests pass.
