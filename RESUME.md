# MSX / MSX2 Handoff

Generated: 2026-06-27 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote branch: `origin/feature/msx2`
Parent checkpoint before this handoff refresh: `bd905ece Update MSX2 resume handoff`

This is the current resume point for the MSX/MSX2 implementation work. The
original Codex session ran for roughly 30 hours and hit practical context-window
limits. Continue from this file and the live worktree state instead of
reconstructing the chat history.

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
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test|mnemos_msx_boot_test" --output-on-failure'
```

## User Contract

- Implement both MSX and MSX2; they share common manifests, mapper code, player
  adapters, VDP behavior, smoke scripts, and golden-test surfaces.
- Preserve the requested worktree and branch: `feature/msx2`.
- C-BIOS lives under `D:\emu\msx\bios`.
- The ROM corpus used for this slice is `D:\emu\msx\MSX files [ROM]`.
- Use `Test-Path -LiteralPath` for paths under `MSX files [ROM]`; brackets are
  wildcard syntax in PowerShell.
- A blank Mnemos Player window is not proof. Player proof must use explicit
  `--system` and `--rom` arguments.
- Do not claim "100% working" until real ROM/media validation proves it.
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
- Focused V9938 and MSX boot tests passed at the last validated checkpoint.
- The branch was clean and aligned with `origin/feature/msx2` before this
  handoff refresh.

Active blocker:

- Skip-192 still fails on MSX2 for `bean.rom`.
- MSX `bean.rom` stays alive and renders a nonuniform framebuffer in the same
  scenario.
- MSX2 `bean.rom` halts at `$CA3E` after a divergent CPU/control-flow path
  around `$99DB`, `$BFFF`, and `$C000`.
- The goal is not complete until this and broader real-ROM coverage pass.

## Important Correction

The earlier handoff interpreted `mode=4` as V9938 Graphics IV. That is wrong
for this codebase.

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

## Latest Reproduced Diagnostics

Recent diagnostics used `bean.rom` for MSX1 and MSX2 with forced SHA mismatch.
The commands intentionally exited 42 because `MNEMOS_*_BOOT_SHA256` was set to
`force-diagnostics`.

Scratch logs generated during the latest investigation:

```text
build\scratch\msx-bean-diagnostics\current-msx-d800-pcwatch.log
build\scratch\msx-bean-diagnostics\current-msx2-d800-pcwatch.log
build\scratch\msx-bean-diagnostics\current-msx2-c000-write-pc8000-8400.log
build\scratch\msx-bean-diagnostics\current-msx2-c000-write-pc8080-8120.log
build\scratch\msx-bean-diagnostics\current-msx-pcwatch-99d0-9a10.log
build\scratch\msx-bean-diagnostics\current-msx2-pcwatch-99d0-9a10.log
build\scratch\msx-bean-diagnostics\current-msx2-99c0-write-allpc.log
build\scratch\msx-bean-diagnostics\current-msx-pcwatch-82f0-8330.log
build\scratch\msx-bean-diagnostics\current-msx2-pcwatch-82f0-8330.log
build\scratch\msx-bean-diagnostics\current-msx-pcwatch-832d.log
build\scratch\msx-bean-diagnostics\current-msx2-pcwatch-832d.log
```

These are scratch artifacts only and are not committed.

MSX1 final forced summary:

```text
cpu pc/sp/af/bc/de/hl: $829D/$E395/$A68C/$0000/$0008/$9C56 halted=false iff1=true iff2=true im=0 cycles=35841613
slot state: primary=$D0 secondary3=$00
ram state: pages16k=[0,0,64,1201]
logical memory state: d800=[$F0,...]
vdp state: frame=600 mode=3 r0=$02 r1=$E2 r2=$06 r7=$04 vram_nonzero=7336 first_pixel=5527021
framebuffer SHA: 690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a
```

MSX2 final forced summary:

```text
cpu pc/sp/af/bc/de/hl: $CA3E/$E6FF/$5FBA/$0101/$5F01/$66B8 halted=true iff1=false iff2=false im=0 cycles=35841602
slot state: primary=$D0 secondary0=$00 secondary1=$00 secondary2=$00 secondary3=$A0
ram mapper segments: [3,2,1,0]
logical memory state: d800=[$00,...,$04,$44,$38]
vdp state: frame=600 mode=4 r0=$02 r1=$E2 r2=$06 r7=$F4 r15=$00 s0=$C4 s1=$01 irq=true vram_nonzero=6104 first_pixel=2368548
framebuffer SHA: 9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573
```

Interpretation:

- The MSX1 path remains alive at PC `$829D`, non-halted, and renders nonuniform
  output.
- The MSX2 path halts at `$CA3E` after a divergent game-control path.
- Final MSX2 VDP mode is Graphics II.

## Exact `$832D` Evidence

The current strongest evidence is the computed jump helper at ROM `$832D`.

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

This confirms MSX2 selects a different object/script path. `$99DB` may be a
valid setup routine in ROM data; do not assume pointer corruption without new
evidence.

## `$99DB` And `$BFFF` Details

ROM bytes around `$99C0-$99F0`:

```text
$99C0: 02 05 00 FF 2F 00 5F 06 80 07 80 00 00 28 A6 DB
$99D0: 99 45 9C 46 9C 56 9C 57 9C 3C A1 21 FD 99 11 40
$99E0: 00 06 06 CD FF BF 21 2D 9A 11 58 00 06 04 CD DF
$99F0: BF 21 4D 9A 11 60 00 06 10 CD FF BF C9 00 00 01
```

`$99DB` begins with a plausible routine:

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

The bad-looking path:

```text
previous_pc=$99E3 current_pc=$BFFF cycles=14564941
prev_code=[$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11,$99DF=$40,$99E0=$00,$99E1=$06,$99E2=$06,$99E3=$CD,$99E4=$FF,$99E5=$BF,$99E6=$21,...]
code=[$BFF7=$01,$BFF8=$08,$BFF9=$00,$BFFA=$09,$BFFB=$C1,$BFFC=$10,$BFFD=$E1,$BFFE=$C9,$BFFF=$C5,$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00]
```

Relevant ROM bytes at CPU `$BFF0`:

```text
$BFC0: BC BF 21 DA BF 06 05 DD 7E 00 BE 30 03 23 10 FA
$BFD0: EB 48 06 00 09 7E DD 77 02 C9 36 2C 24 1F 1C C5
$BFE0: E5 D5 E5 EB 29 29 29 11 00 38 19 EB E1 01 08 00
$BFF0: CD 7D 80 D1 13 13 E1 01 08 00 09 C1 10 E1 C9 C5
```

The ROM executes `CALL $BFFF`, pushes return `$99E6`, executes `$BFFF=$C5`
(`PUSH BC`), then falls into page-3 RAM at `$C000`. `$BFFF=$C5` is plausible
because `bean.rom` is a 16 KiB upper-page plain ROM mirrored into
`$8000-$BFFF`. Do not "fix" this by unmapping `$BFFF` unless new evidence proves
the mapper should behave differently.

`current-msx2-99c0-write-allpc.log` contains no `memory watch` section/events
for `$99C0-$99E0`. That means the pointer area around `$99CF` appears to be ROM
data, not RAM corruption.

## `$C000` Data Clue

The `$C000` write trace reconstructed from
`build\scratch\msx-bean-diagnostics\current-msx2-c000-write-pc8080-8120.log`:

```text
events 256
809F 32 C000-C01F first 00 00 27 03 27 03 27 03 ... last 27 03 ...
80E1 75 C000-C014 first 00 27 17 17 17 37 ... last 64 64 64 64 64 00 27 17 03 13 33 55 77 74 63 73
80F7 75 C000-C014 first 00 27 17 16 16 36 ... last 63 63 63 63 63 00 27 17 02 12 33 55 77 74 63 73
8106 74 C001-C013 first 00 03 02 02 ... last 05 05 06 07 04 02 00 03 01 00 01 03 05 07 06 05
C000: 00 00 27 03 17 01 02 00 12 01 33 03 55 05 77 07
C010: 74 06 63 05 73 05 63 05 63 06 63 07 63 04 63 02
```

Existing watch cap is 256, so it may miss later events, but this confirms
`$C000` is data-like by the time the `$BFFF` fallthrough occurs.

## Relevant Code Already Inspected

- `src/manifests/msx2/msx2_system.cpp`
- `src/manifests/msx/msx_system.cpp`
- `src/manifests/common/msx_io_ports.hpp`
- `src/manifests/common/msx_io_ports.cpp`
- `src/manifests/common/msx_cartridge_mapper.cpp`
- `src/chips/video/v9938/v9938.cpp`
- `src/chips/video/v9938/v9938.hpp`
- `src/chips/video/tms9918a/tms9918a.cpp`
- `src/chips/cpu/z80/z80.cpp`
- `tests/golden/msx_boot_test.cpp`

Important code facts:

- MSX2 `$FFFF` read/write is active when selected page-3 primary slot is
  expanded; reads return `secondary_slot ^ 0xFF`, writes store the raw value.
- MSX2 I/O `$98/$99` routes to VDP data/status.
- MSX2 I/O `$A8` routes to the primary slot latch.
- MSX2 I/O `$FC-$FF` routes to RAM mapper segments.
- Final failing MSX2 state has primary `$D0`, secondary3 `$A0`, and RAM mapper
  segments `[3,2,1,0]`.
- `msx_ppi_port_a_output(control)` is `(control & 0x10) == 0`; reset
  `ppi_control=$9B`, so port A is input at reset.
- V9938 `status_read()` selects `R#15 & 0x0F`; selected S#0 clears only the
  frame IRQ bit in the current branch.
- Z80 implementation areas to inspect if evidence points there:
  - `EXX`
  - `JP (HL)`
  - `DJNZ`
  - DD/FD indexed paths
  - stack push/pop

## Likely Root-Cause Areas

Current evidence favors one of these:

1. V9938 status/timing/IRQ behavior is causing the MSX2-only game path to
   schedule the `$99DB` script.
2. The `$99DB` routine is valid, and the actual fault is in the helper/fallthrough
   path at `$BFFF/$C000`, stack state, or the generated data at `$C000`.
3. A narrow Z80 bug affects this stack/fallthrough/computed-jump sequence.
4. Slot or RAM mapper page-3 selection is wrong at the moment `$BFFF` falls into
   `$C000`.

Do not make broad mapper or VDP rendering changes without a diagnostic that
selects one of these.

## Best Next Actions

1. Trace `$BFFF-$C020` more deeply with stack and IX context:

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
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx2]'
```

2. If existing PC watch output is insufficient, add a gated diagnostic in
   `tests/golden/msx_boot_test.cpp` that prints `ix_window=` and `sp_window=`
   for PC watch hits. Keep it only if generally useful.
3. Compare MSX1/MSX2 around `$82F8-$832D` and identify why MSX2 reaches the
   fourth `$832D` call with `BC=$FFA1`, `IX=$99CF`, and `HL=$99DB`.
4. Inspect V9938 status timing only through a narrow hypothesis: does an MSX2
   status value select the `$99DB` script while MSX1 avoids it?
5. After any candidate fix, run the targeted DevCmd build/test command above,
   then rerun the real-ROM skip-192 smoke with explicit MSX/MSX2 ROM and BIOS
   arguments.

## Completion Bar

Do not mark the feature complete until all of these are true:

- `bean.rom` no longer halts on MSX2 and produces a stable nonblank/nonuniform
  framebuffer.
- MSX and MSX2 C-BIOS golden tests pass under the Windows preset.
- The bounded real-ROM smoke advances beyond the current skip-192 blocker.
- Player validation launches with explicit `--system` and `--rom` arguments and
  renders real game output, not a blank default window.
- The final report states exactly which ROMs, BIOS files, commands, and frame
  counts were used.

## Last Handoff Action

This handoff refresh only updated `RESUME.md`; it did not implement a semantic
emulation fix and did not rerun the Windows validation suite.
