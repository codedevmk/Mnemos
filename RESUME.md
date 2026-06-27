# MSX / MSX2 Handoff

Generated: 2026-06-27 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote branch: `origin/feature/msx2`
Parent checkpoint before this handoff refresh: `54ac0408 Update MSX2 resume handoff`

This is the current resume point for the MSX/MSX2 implementation work. The
original Codex session ran for roughly 30 hours and hit practical context-window
limits. Continue from this file and the live worktree state instead of trying to
reconstruct the chat history.

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

Active blocker:

- Skip-192 still fails on MSX2 for `bean.rom`.
- MSX `bean.rom` stays alive and renders a nonuniform framebuffer in the same
  scenario.
- MSX2 `bean.rom` reaches a bad halted CPU/control-flow state around
  `$BFFF/$C000`.
- The goal is not complete until this and broader real-ROM coverage pass.

## Important Correction

The previous handoff interpreted `mode=4` as V9938 Graphics 4. That was wrong
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

## Latest Evidence

The most recent forced diagnostics used `bean.rom` for MSX1 and MSX2 with VDP
I/O watch around the ROM setup range:

```text
build\scratch\msx-bean-diagnostics\resume-msx-vdp-io-watch-82f0-8340.log
build\scratch\msx-bean-diagnostics\resume-msx2-vdp-io-watch-82f0-8340.log
```

These logs are scratch artifacts only and are not committed.

MSX1 final forced summary:

```text
cpu pc/sp/af/bc/de/hl: $829D/$E395/$A68C/$0000/$0008/$9C56 halted=false iff1=true iff2=true im=0 cycles=35841613
slot state: primary=$D0 secondary3=$00
ram state: pages16k=[0,0,64,1201]
logical memory state: d800=[$F0,...] e0e0=... f390=... fd90=...
vdp state: frame=600 mode=3 r0=$02 r1=$E2 r2=$06 r7=$04 vram_nonzero=7336 first_pixel=5527021
framebuffer SHA: 690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a
```

MSX2 final forced summary:

```text
cpu pc/sp/af/bc/de/hl: $CA3E/$E6FF/$5FBA/$0101/$5F01/$66B8 halted=true iff1=false iff2=false im=0 cycles=35841602
slot state: primary=$D0 secondary3=$A0
ram state: pages16k=[6962,64,0,0,...]
ram mapper segments: [3,2,1,0]
logical memory state: d800=[$00,...,$04,$44,$38] e0e0=... f390=... fd90=...
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
$env:MNEMOS_MSX_VDP_IO_WATCH='1'
$env:MNEMOS_MSX_VDP_IO_WATCH_PC='$82F0-$8340'
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
$env:MNEMOS_MSX_VDP_IO_WATCH='1'
$env:MNEMOS_MSX_VDP_IO_WATCH_PC='$82F0-$8340'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe '[golden][msx2]'
```

Both commands intentionally exited 42 because `MNEMOS_*_BOOT_SHA256` was forced
to a mismatch.

## Existing Diagnostic Clues

The existing PC watch around the bad path showed:

```text
previous_pc=$99E3 current_pc=$BFFF cycles=14564941
prev_code=[$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11,$99DF=$40,$99E0=$00,$99E1=$06,$99E2=$06,$99E3=$CD,$99E4=$FF,$99E5=$BF,$99E6=$21,...]
code=[$BFF7=$01,$BFF8=$08,$BFF9=$00,$BFFA=$09,$BFFB=$C1,$BFFC=$10,$BFFD=$E1,$BFFE=$C9,$BFFF=$C5,$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00]
```

The ROM executes `CALL $BFFF`, reads `$BFFF=$C5` (`PUSH BC`), then falls through
into `$C000` RAM.

Existing memory watch around staging showed:

- First `$C000` staging happens around `$82DC/$8321`.
- Page 3 is slot `3.2`, primary `$C0`, secondary3 `$A0`, RAM segments
  `[3,2,1,0]`.
- The code writes a zero block and later nonzero staged bytes.
- Later there are no memory writes from `$99D0-$C100`, but PC enters
  `$BFFF/$C000`.

Be careful with conclusions here. `$BFFF=$C5` may be correct for a 16 KiB plain
ROM mirrored into `$8000-$BFFF`. The real question is why the subsequent `$C000`
staged payload is wrong, incomplete, or selected differently on MSX2 than MSX1.

## Relevant Code Already Inspected

- `CONSTITUTION.md`
- `README.md`
- `RESUME.md`
- `src/chips/video/v9938/v9938.cpp`
- `src/chips/video/v9938/v9938.hpp`
- `src/chips/video/v9938/tests/v9938_test.cpp`
- `src/chips/video/tms9918a/tms9918a.cpp`
- `src/runtime/scheduler.cpp`
- `src/runtime/scheduler.hpp`
- `tests/golden/msx_boot_test.cpp`
- `src/manifests/msx2/msx2_system.hpp`
- `src/manifests/msx2/msx2_system.cpp`
- `src/manifests/msx2/tests/msx2_system_test.cpp`
- `src/manifests/msx/msx_system.cpp`

Useful V9938 notes:

- `finish_scanline()` renders visible scanlines during `tick(...)`.
- `render_frame()` loops `render_scanline(...)`.
- `framebuffer()` returns the current buffer.
- This is not the current primary root cause because final `bean.rom` mode is
  Graphics II and the MSX2 CPU is halted on the bad path.

Useful MSX2 slot notes from `src/manifests/msx2/msx2_system.cpp`:

- `slot_for_page`: page is `address >> 14`; primary slot is selected from
  `primary_slot >> (page * 2)`.
- `secondary_for_page`: if the primary slot is expanded, the secondary slot is
  selected from `secondary_slot[slot] >> (page * 2)`.
- `read_ram` and `write_ram`: physical RAM segment is
  `ram_segment[page] % segment_count`.
- `read_cartridge`: plain mapper returns `$FF` outside `$4000-$BFFF` unless the
  ROM is larger than 32 KiB; within `$4000-$BFFF`, it uses
  `msx_plain_rom_physical_offset(...)`.
- Final failing state has `primary=$D0`: page 0 slot 0 BIOS, page 1 slot 0 BIOS,
  page 2 slot 1 cartridge, page 3 slot 3 RAM.

## Recommended Next Slice

1. Continue reading `src/manifests/msx2/msx2_system.cpp` around `read_slot`,
   `write_slot`, `cpu_read`, `cpu_write`, `io_read`, `io_write`, and
   `assemble_msx2`.
2. Compare `src/manifests/msx/msx_system.cpp` memory behavior, especially
   `plain_32k_handoff_cart_slot`, `plain_16k_lower_page_visible`, and plain
   <=16 KiB ROM handling.
3. Inspect `src/manifests/common/msx_cartridge_mapper.cpp` and tests for 16 KiB
   plain mirroring around `$BFFF`.
4. Disassemble or byte-trace `bean.rom` around `$82D4-$8349`, `$99D0-$9A10`,
   `$BFF0-$C080`, and the staged `$C000` payload. Keep generated output under
   `build\scratch`.
5. Compare MSX1 and MSX2 values at ROM-used work areas. The suspicious current
   difference is `d800`: MSX1 starts with `$F0`, while MSX2 has `$00` and later
   `$04,$44,$38` in the sampled window.
6. Add or refine opt-in diagnostics only if needed. Remove or keep them gated
   before commit; do not add always-on logging.
7. After a candidate fix, rerun the skip-192 smoke window and then broaden the
   corpus window.

Immediate inspection commands:

```powershell
Get-Content src\manifests\msx2\msx2_system.cpp | Select-Object -Skip 520 -First 280
Get-Content src\manifests\msx2\msx2_system.cpp | Select-Object -Skip 800 -First 300
Get-Content src\manifests\common\msx_cartridge_mapper.cpp | Select-Object -Skip 220 -First 90
Get-Content src\manifests\common\tests\msx_cartridge_mapper_test.cpp | Select-Object -Skip 240 -First 50
```

Targeted build/test command:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test|mnemos_msx_boot_test" --output-on-failure'
```

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

## Completion Bar

Do not mark the MSX/MSX2 goal complete until:

- `bean.rom` passes both MSX and MSX2 in the skip-192 smoke window.
- The fix has focused regression coverage in chip/system/golden tests as
  appropriate.
- Explicit player launches use `--system` and `--rom` and render non-blank,
  game-specific output.
- A broader MSX/MSX2 corpus window passes after the skip-192 fix.
- The Windows MSVC preset build and relevant tests pass.
