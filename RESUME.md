# MSX / MSX2 Resume Handoff

Generated: 2026-06-27 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote branch: `origin/feature/msx2`
Parent checkpoint before this handoff refresh: `8880b53b Refresh MSX2 resume handoff`

This file is the authoritative resume point for the MSX/MSX2 implementation
thread. The original session ran for roughly 30 hours and is no longer a useful
context source. Continue from this file, the live worktree, and executable
evidence.

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
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test mnemos_chips_video_v9938_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test|mnemos_msx_boot_test|mnemos_chips_video_v9938_test" --output-on-failure'
```

Repository rules that matter most here:

- Read `CONSTITUTION.md` and `README.md` before toolchain work.
- Product code lives under `src/`; build, log, and diagnostic output belongs
  under `build/`.
- Do not commit ROMs, firmware, screenshots, logs, build outputs, or generated
  scratch artifacts.
- Other emulators are reference material only; do not lift code.

## User Contract

- Implement both MSX and MSX2; they share common manifests, cartridge mapper
  code, player adapters, VDP behavior, smoke scripts, and golden-test surfaces.
- Preserve the requested worktree and branch: `feature/msx2`.
- C-BIOS root: `D:\emu\msx\bios`.
- C-BIOS files verified during this session:
  - `D:\emu\msx\bios\cbios\cbios_main_msx1.rom`
  - `D:\emu\msx\bios\cbios\cbios_logo_msx1.rom`
  - `D:\emu\msx\bios\cbios\cbios_main_msx2.rom`
  - `D:\emu\msx\bios\cbios\cbios_sub.rom`
  - `D:\emu\msx\bios\cbios\cbios_logo_msx2.rom`
- ROM corpus used for this slice: `D:\emu\msx\MSX files [ROM]`.
- Use `Test-Path -LiteralPath` for paths under `MSX files [ROM]`; brackets are
  wildcard syntax in PowerShell.
- A blank Mnemos Player window is not proof. Player proof must use explicit
  `--system` and `--rom` arguments.
- Do not claim "100% working" until real ROM/media validation proves it.

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- C-BIOS MSX and MSX2 boot paths are wired through golden tests and explicit
  player launch paths.
- Shared RAM-size profile semantics, cartridge mapper resolution, smoke-script
  routing, and MSX/MSX2 golden-test hooks are in place.
- Earlier bounded real-ROM smoke windows passed through skip 191.
- Focused V9938 and MSX boot tests passed at the last validated checkpoint
  before the temporary S#0 experiment described below.
- The branch was clean and aligned with `origin/feature/msx2` before this
  handoff refresh.

Active blocker:

- Skip-192 still fails on MSX2 for `bean.rom`.
- MSX `bean.rom` stays alive and renders a nonuniform framebuffer in the same
  scenario.
- MSX2 `bean.rom` halts at `$CA3E` after divergent control flow around
  `$832D`, `$99DB`, `$BFFF`, and `$C000`.
- The goal remains open until this blocker and broader real-ROM coverage pass.

## Current Worktree State

At the start of this handoff refresh:

```text
C:\dev\emu\Mnemos-msx2
feature/msx2
HEAD 8880b53b
status: clean, tracking origin/feature/msx2
```

Only `RESUME.md` should be changed by this handoff commit. If any other source
file is dirty, inspect it before continuing; do not use destructive git reset or
checkout to discard user work.

## Latest Diagnostic Evidence

The target ROM:

```text
path: D:\emu\msx\MSX files [ROM]\bean.rom
size: 16384
sha256: 7e193f203d6b327689ec4b65681a7b1a868756d942c2cc03f381878e12d8edb8
```

The latest diagnostics used forced SHA mismatch so the test executable would
dump detailed state. Exit code `42` was intentional.

MSX2 command environment for the strongest `$BFFF-$C020` trace:

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
$env:MNEMOS_MSX_PC_WATCH='$BFFF-$C020'
$env:MNEMOS_MSX_MEM_WATCH='$C000-$C03F'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe
```

Scratch logs from this phase were written under:

```text
build\scratch\msx-bean-diagnostics\
```

They are intentionally not committed.

## Important Correction

An earlier handoff interpreted `mode=4` as V9938 Graphics IV. That is wrong for
this codebase.

In `src/chips/video/v9938/v9938.hpp`, the enum order is:

```text
graphics_i=0, text_i=1, text_ii=2, multicolor=3, graphics_ii=4,
graphics_iii=5, graphics_iv=6, ...
```

For failing `bean.rom`, final MSX2 VDP state is:

```text
r0=$02 r1=$E2 mode=4
```

That decodes to TMS-compatible Graphics II, not V9938 bitmap/Graphics IV. The
immediate blocker is more likely CPU/control-flow, slot/RAM/work-area behavior,
or VDP status/timing selecting the wrong game script, not Graphics IV page
rendering.

## `$832D`, `$99DB`, `$BFFF`, `$C000`

The strongest current evidence is the ROM dispatcher at `$832D`.

MSX2 reaches `$832D` four times in the 600-frame diagnostic:

```text
cycles=14093336 af=$8D84 bc=$0000 de=$0002 hl=$833C ix=$8330 iy=$0184 sp=$E3B1 ret0=$831C
cycles=14563107 af=$9488 bc=$0001 de=$0002 hl=$8D8A ix=$8D7E iy=$0184 sp=$E3B1 ret0=$831C
cycles=14563449 af=$9984 bc=$0001 de=$0002 hl=$9471 ix=$9465 iy=$0184 sp=$E3B1 ret0=$831C
cycles=14564893 af=$A68C bc=$FFA1 de=$0002 hl=$99DB ix=$99CF iy=$0184 sp=$E3B1 ret0=$831C
```

The fourth call jumps to `$99DB`:

```text
previous_pc=$832D current_pc=$99DB cycles=14564897
code=[$99D3=$46,$99D4=$9C,$99D5=$56,$99D6=$9C,$99D7=$57,$99D8=$9C,$99D9=$3C,$99DA=$A1,$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11,$99DF=$40,$99E0=$00,$99E1=$06,$99E2=$06]
```

MSX1 has zero hits for exact `current_pc=$832D` in the same 600-frame window.
This confirms MSX2 selects a different object/script path.

ROM bytes around `$99C0-$99F0`:

```text
$99C0: 02 05 00 FF 2F 00 5F 06 80 07 80 00 00 28 A6 DB
$99D0: 99 45 9C 46 9C 56 9C 57 9C 3C A1 21 FD 99 11 40
$99E0: 00 06 06 CD FF BF 21 2D 9A 11 58 00 06 04 CD DF
$99F0: BF 21 4D 9A 11 60 00 06 10 CD FF BF C9 00 00 01
```

`$99DB` begins with plausible code:

```text
LD HL,$99FD
LD DE,$0040
LD B,$06
CALL $BFFF
LD HL,$9A2D
LD DE,$0058
LD B,$04
CALL $BFDF
LD HL,$9A4D
LD DE,$0060
LD B,$10
CALL $BFFF
RET
```

The current bad-looking path:

```text
previous_pc=$99E3 current_pc=$BFFF cycles=14564941
af=$A68C bc=$06A1 de=$0040 hl=$99FD ix=$99CF iy=$0184 sp=$E3AF ret0=$99E6
prev_code=[$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11,$99DF=$40,$99E0=$00,$99E1=$06,$99E2=$06,$99E3=$CD,$99E4=$FF,$99E5=$BF,$99E6=$21,...]
code=[$BFF7=$01,$BFF8=$08,$BFF9=$00,$BFFA=$09,$BFFB=$C1,$BFFC=$10,$BFFD=$E1,$BFFE=$C9,$BFFF=$C5,$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00]
```

Then:

```text
previous_pc=$BFFF current_pc=$C000 cycles=14564952
af=$A68C bc=$06A1 de=$0040 hl=$99FD ix=$99CF iy=$0184 sp=$E3AD ret0=$06A1
```

Interpretation:

- `CALL $BFFF` pushes return `$99E6`.
- `$BFFF=$C5` (`PUSH BC`) pushes `$06A1`, so top-of-stack becomes `$06A1`.
- PC enters `$C000`, where the current page-3 bytes are data-like:
  `$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00`.
- Execution continues through data-like bytes and eventually halts at `$CA3E`.
- This confirms the `$BFFF/$C000` consequence, but the wrong decision likely
  happens earlier around `$832D/$99DB`.

Do not "fix" this by blindly mapping 16 KiB plain ROM into page 3. Existing
tests intentionally include C-BIOS handoff coverage for non-32 KiB plain
cartridges remaining unmapped outside their valid window.

## VDP S#0 Experiment

A temporary experiment changed `src/chips/video/v9938/v9938.cpp` S#0 status
read behavior from clearing only the frame IRQ bit to:

```cpp
status_[0] &= 0x1FU;
```

This was built and tested with:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

Result:

- `mnemos_msx_boot_test` passed default tests.
- `mnemos_chips_video_v9938_test` failed the existing tests that require sprite
  overflow/collision bits to survive S#0 reads.
- The experiment did not fix `bean.rom`; MSX2 still halted at `$CA3E` with the
  same framebuffer hash:
  `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573`.
- The experiment was reverted. `src/chips/video/v9938/v9938.cpp` should keep:

```cpp
status_[0] &= static_cast<std::uint8_t>(~k_status_frame_irq);
```

Before continuing, verify this file is clean with `git status`.

## Slot And Mapper Context

Files already inspected:

- `src/manifests/msx2/msx2_system.cpp`
- `src/manifests/msx/msx_system.cpp`
- `src/manifests/common/msx_io_ports.hpp`
- `src/manifests/common/msx_io_ports.cpp`
- `src/manifests/common/msx_cartridge_mapper.cpp`
- `src/chips/video/v9938/v9938.cpp`
- `src/chips/video/v9938/v9938.hpp`
- `tests/golden/msx_boot_test.cpp`
- `tests/golden/msx_rom_profiles.json`
- `scripts/msx/run-boot-smoke.ps1`

Relevant profile clue:

- `tests/golden/msx_rom_profiles.json` has ROM-specific overrides for BIOS,
  mapper, sub-slot, RAM slot, boot frames, and reasons.
- Existing `BARBARIAN.rom` profile uses `sub_slot: 3.1` and `ram_slot: 3.0`
  because the default `3.0/3.2` profile leaves it executing from an unmapped RAM
  page.
- `bean.rom` currently has no profile entry.

The next high-value path is to test `bean.rom` under a small C-BIOS/slot-layout
matrix before changing mapper semantics.

## Next Diagnostic Matrix

Run a compact matrix for `bean.rom` with forced diagnostics:

- BIOS variants:
  - `D:\emu\msx\bios\cbios\cbios_main_msx2.rom`
  - `D:\emu\msx\bios\cbios\cbios_main_msx2_eu.rom`
  - `D:\emu\msx\bios\cbios\cbios_main_msx2_br.rom`
  - `D:\emu\msx\bios\cbios\cbios_main_msx2_jp.rom`
- Slot layouts:
  - current: `MNEMOS_MSX2_SUB_SLOT=3.0`, `MNEMOS_MSX2_RAM_SLOT=3.2`
  - alternate: `MNEMOS_MSX2_SUB_SLOT=3.1`, `MNEMOS_MSX2_RAM_SLOT=3.0`
  - defaults/no overrides if the harness supports that cleanly
- Mapper: leave automatic/plain unless evidence says otherwise.
- Frames: 600 first; increase only after a promising profile keeps execution
  alive.
- Capture final PC, halted flag, framebuffer SHA, uniform/nonuniform, mode,
  slot state, and whether `$832D/$99DB/$BFFF/$C000` repeats.

If one profile fixes `bean.rom`, add a targeted profile entry to
`tests/golden/msx_rom_profiles.json` with:

```text
sha256: 7e193f203d6b327689ec4b65681a7b1a868756d942c2cc03f381878e12d8edb8
mapper: plain or the proven mapper
bios/sub_slot/ram_slot overrides: only the proven minimum
boot_frames: proven frame count
reason: explain the exact slot/BIOS compatibility requirement
```

Then run a bounded corpus smoke through and beyond skip 192.

## Smoke Runner Reminder

Inspect `scripts/msx/run-boot-smoke.ps1` before using it. A likely invocation
shape is:

```powershell
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir build/windows-msvc-debug `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2RomDir 'D:\emu\msx\MSX files [ROM]' `
  -RomProfileManifest tests/golden/msx_rom_profiles.json `
  -Frames 600 `
  -SkipRoms 191 `
  -MaxRoms 16
```

Adjust parameters based on the script's actual help and current source.

## If The Matrix Does Not Fix It

Continue tracing the scheduler/control path rather than changing VDP rendering
or broad mapper behavior.

Recommended next trace additions:

- Add gated PC-watch enrichment in `tests/golden/msx_boot_test.cpp` that dumps
  `ix_window=` and `sp_window=` around watch hits.
- Run exact `$832D` PC watch with stack and IX windows to understand why the
  fourth dispatch uses `IX=$99CF` and `HL=$99DB`.
- Trace calls to `$829D`, `$823D`, and memory variables `$E12D/$E3C5`.
- Keep diagnostics under `build\scratch\msx-bean-diagnostics\`.

## Validation Before Claiming Progress

For code changes in this slice, use the targeted build/test sweep:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test mnemos_chips_video_v9938_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test|mnemos_msx_boot_test|mnemos_chips_video_v9938_test" --output-on-failure'
```

For player proof, launch with explicit system and ROM arguments. A player window
without `--system` and `--rom` only proves the player starts.

## Stop Condition

Do not mark the MSX/MSX2 goal complete until:

- `bean.rom` MSX2 skip-192 is understood and either fixed or correctly profiled.
- Bounded smoke passes through the previous failure point and records hashes.
- MSX and MSX2 both have explicit real-ROM proof, not just unit tests.
- Any code changes are covered by focused tests plus the targeted Windows
  validation sweep.
