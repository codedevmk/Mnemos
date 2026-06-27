# MSX / MSX2 Handoff

Generated: 2026-06-27 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote branch: `origin/feature/msx2`
Parent checkpoint before this handoff refresh: `e33b8c7141d75743c495115426f2afc5379c6dbc`

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
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test|mnemos_msx_boot_test" --output-on-failure'
```

## User Contract

- Implement both MSX and MSX2; they share common manifests, mapper code, player
  adapters, VDP behavior, smoke scripts, and golden-test surfaces.
- Preserve the requested worktree and branch: `feature/msx2`.
- C-BIOS lives under `D:\emu\msx\bios`.
- The ROM corpus used for this slice is `D:\emu\msx\MSX files [ROM]`.
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
- The current branch is synced with `origin/feature/msx2` before this handoff
  refresh.

Active blocker:

- Skip-192 still fails on MSX2 for `bean.rom`.
- MSX `bean.rom` stays alive and renders a nonuniform framebuffer in the same
  scenario.
- MSX2 `bean.rom` reaches a bad halted CPU/control-flow state around
  `$BFFF/$C000`.
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
`visible_g4_*` diagnostics in `tests/golden/msx_boot_test.cpp` are misleading
for this run because the current mode is Graphics II. Do not chase Graphics IV
framebuffer/page selection as the primary root cause unless new evidence shows
the ROM should finish in Graphics IV.

## Latest Reproduced Diagnostics

Recent diagnostics used `bean.rom` for MSX1 and MSX2 with forced SHA mismatch.
The commands intentionally exited 42 because `MNEMOS_*_BOOT_SHA256` was set to
`force-diagnostics`.

Scratch logs generated in this continuation:

```text
build\scratch\msx-bean-diagnostics\current-msx-d800-pcwatch.log
build\scratch\msx-bean-diagnostics\current-msx2-d800-pcwatch.log
build\scratch\msx-bean-diagnostics\current-msx2-c000-write-pc8000-8400.log
build\scratch\msx-bean-diagnostics\current-msx2-c000-write-pc8080-8120.log
build\scratch\msx-bean-diagnostics\current-msx-pcwatch-99d0-9a10.log
build\scratch\msx-bean-diagnostics\current-msx2-pcwatch-99d0-9a10.log
build\scratch\msx-bean-diagnostics\current-msx2-99c0-write-allpc.log
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
- The MSX2 path halts at `$CA3E` after bad control flow through the staged
  `$BFFF/$C000` path.
- Final MSX2 VDP mode is Graphics II, so the immediate blocker is more likely
  slot/RAM/work-area/control-flow behavior than V9938 Graphics IV rendering.

## Commands Already Used

MSX1 forced diagnostic:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_ROM='D:\emu\msx\MSX files [ROM]\bean.rom'
$env:MNEMOS_MSX_BOOT_FRAMES='600'
$env:MNEMOS_MSX_BOOT_SHA256='force-diagnostics'
$env:MNEMOS_MSX_D800_WATCH='1'
$env:MNEMOS_MSX_PC_WATCH='$BFF0-$C080'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx]'
```

MSX2 forced diagnostic:

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
$env:MNEMOS_MSX_D800_WATCH='1'
$env:MNEMOS_MSX_PC_WATCH='$BFF0-$C080'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx2]'
```

MSX2 `$C000` write trace:

```powershell
$env:MNEMOS_MSX_MEM_WATCH='$C000-$C03F'
$env:MNEMOS_MSX_MEM_WATCH_PC='$8080-$8120'
$env:MNEMOS_MSX_PC_WATCH='$BFF0-$C080'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx2]'
```

MSX/MSX2 PC watch `$99D0-$9A10`:

```powershell
$env:MNEMOS_MSX_PC_WATCH='$99D0-$9A10'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx]'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx2]'
```

## Current Diagnostic Clues

The bad path:

```text
previous_pc=$99E3 current_pc=$BFFF cycles=14564941
prev_code=[$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11,$99DF=$40,$99E0=$00,$99E1=$06,$99E2=$06,$99E3=$CD,$99E4=$FF,$99E5=$BF,$99E6=$21,...]
code=[$BFF7=$01,$BFF8=$08,$BFF9=$00,$BFFA=$09,$BFFB=$C1,$BFFC=$10,$BFFD=$E1,$BFFE=$C9,$BFFF=$C5,$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00]
```

The relevant ROM bytes at CPU `$BFF0` are:

```text
$BFF0: CD 7D 80 D1 13 13 E1 01 08 00 09 C1 10 E1 C9 C5
```

The ROM executes `CALL $BFFF`, pushes return `$99E6`, executes `$BFFF=$C5`
(`PUSH BC`), then falls into page-3 RAM at `$C000`. `$BFFF=$C5` is plausible
because `bean.rom` is a 16 KiB upper-page plain ROM mirrored into
`$8000-$BFFF`. Do not "fix" this by unmapping `$BFFF` unless new evidence proves
the mapper should behave differently.

The higher-level discovery:

- The entry into `$99DB` is a computed `JP (HL)` from ROM `$832D`.
- MSX2 PC watch `$99D0-$9A10` showed:

```text
previous_pc=$832D current_pc=$99DB cycles=14564897 af=$A68C bc=$FFA1 de=$0002 hl=$99DB ix=$99CF iy=$0184 sp=$E3B1 ret0=$831C
prev_code=[$8325=$DD,$8326=$66,$8327=$01,$8328=$E5,$8329=$DD,$832A=$E1,$832B=$18,$832C=$CF,$832D=$E9,...]
code=[$99D3=$46,$99D4=$9C,$99D5=$56,$99D6=$9C,$99D7=$57,$99D8=$9C,$99D9=$3C,$99DA=$A1,$99DB=$21,$99DC=$FD,$99DD=$99,...]
```

- MSX1 does not hit `$99D0-$9A10` in the same 600-frame window.
- MSX2 is feeding the game a bad object/script pointer (`IX=$99CF` gives pointer
  `$99DB`), not merely mishandling `$BFFF`.

ROM bytes around `$99C0-$99F0`:

```text
$99C0: 02 05 00 FF 2F 00 5F 06 80 07 80 00 00 28 A6 DB
$99D0: 99 45 9C 46 9C 56 9C 57 9C 3C A1 21 FD 99 11 40
$99E0: 00 06 06 CD FF BF 21 2D 9A 11 58 00 06 04 CD DF
$99F0: BF 21 4D 9A 11 60 00 06 10 CD FF BF C9 00 00 01
```

`IX=$99CF` points at bytes `DB 99`, which decode as pointer `$99DB`.

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
`$C000` is data-like.

## ROM And Search Notes

`bean.rom` facts:

- Use `Test-Path -LiteralPath 'D:\emu\msx\MSX files [ROM]\bean.rom'`; plain
  `Test-Path` is wrong because brackets in `MSX files [ROM]` are wildcards.
- Size is 16384 bytes.
- Header starts `41 42`.
- Init vector is `$8004`.
- Mapper resolves as Plain.

Search context:

- `E3C5` appears only as the store at `$8305`: `IN A,($99); LD ($E3C5),A`.
- No ROM load from `$E3C5` was found.
- `99DB` appears at `$8303` as `DB 99` (I/O read), at `$99CF` as pointer/data,
  and at `$B6A3` as another `IN A,($99)`.
- `$832D` is reached from `$8319` (`CALL $832D`) and contains `E9` (`JP (HL)`).
- A conditional jump opcode at file `$322A` / CPU `$B22A` is `E2 -> $9A18`,
  but this is not directly `$99DB`.

## Relevant Code Already Inspected

- `CONSTITUTION.md`
- `README.md`
- `RESUME.md`
- `src/manifests/msx2/msx2_system.hpp`
- `src/manifests/msx2/msx2_system.cpp`
- `src/manifests/msx/msx_system.cpp`
- `src/manifests/msx2/tests/msx2_system_test.cpp`
- `src/manifests/msx/tests/msx_system_test.cpp`
- `src/manifests/common/msx_cartridge_mapper.cpp`
- `src/manifests/common/tests/msx_cartridge_mapper_test.cpp`
- `src/manifests/common/msx_io_ports.hpp`
- `src/chips/video/v9938/v9938.cpp`
- `src/chips/video/v9938/v9938.hpp`
- `src/chips/video/v9938/tests/v9938_test.cpp`
- `src/chips/video/tms9918a/tms9918a.cpp`
- `src/chips/video/tms9918a/tests/tms9918a_test.cpp`
- `tests/golden/msx_boot_test.cpp`

## Code Facts From Inspection

MSX2 slot decode:

- `slot_for_page`: `(primary_slot >> (page * 2)) & 3`
- `secondary_for_page`: only if `expanded_slot[slot]`,
  `(secondary_slot[slot] >> (page * 2)) & 3`
- `$FFFF` read/write is active when selected page-3 primary slot is expanded;
  reads return `secondary_slot ^ 0xFF`, writes store raw `secondary_slot=value`.
- Tests currently assume writes are raw and reads are inverted.

MSX2 default slot layout:

- Slot 0 is expanded internal ROMs; subslot 0 is main BIOS/logo, subslot 1 is
  sub-ROM by default.
- Slot 1 is cartridge.
- Slot 3 is mapper RAM.
- Current C-BIOS profile uses `expanded=8`, `sub=3.0`, `ram=3.2`.

RAM mapper:

- Shared helper returns initial pages `{3,2,1,0}`.
- `ram_segment[3]=0`, so logical page 3 `$C000-$FFFF` maps raw RAM segment 0.
- This is already tested.

V9938 status read:

- Selected register comes from `R#15 & 0x0F`.
- Selected S#0 clears only the frame IRQ bit in the current branch.
- TMS `status_read()` clears `status_ &= 0x1F`.
- Earlier V9938 S#0 preservation did not fix the blocker.

## Next Steps

1. Parse `build\scratch\msx-bean-diagnostics\current-msx2-99c0-write-allpc.log`
   to see whether the pointer table around `$99CF` is RAM-written or just ROM
   data. If no memory-watch events exist, `$99CF` is ROM data and the question
   becomes why the object scheduler selects that slot.
2. Trace the computed pointer scheduler around `$82F8-$832D` with a narrow PC
   watch. Compare MSX1 and MSX2 at the same range and capture `IX`, `HL`, `IY`,
   stack return, and bytes at `IX`.
3. If existing watch output is insufficient, add a temporary opt-in trace at PC
   `$832D` gated behind an environment variable. Remove it before commit unless
   it is generally useful and covered.
4. Investigate why MSX2 reaches object pointer `$99DB` while MSX1 does not. VDP
   status timing/S#0 bits remain suspicious, but do not assume they are the
   cause.
5. Avoid broad mapper changes. Current evidence supports 16 KiB plain ROM mirror
   behavior at `$BFFF`.
6. After any candidate fix, run the targeted DevCmd build/test command above,
   then run the real-ROM skip-192 smoke with explicit MSX/MSX2 ROM and BIOS
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
