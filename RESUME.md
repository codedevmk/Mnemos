# MSX / MSX2 Resume Handoff

Generated: 2026-06-25T16:16:41-05:00
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote tracking branch: `origin/feature/msx2`
Previous pushed commit before this handoff update: `164fb612769b338ca21189336f6775c0b79ebd65` (`Add MSX and MSX2 emulation snapshot`)

## User Goal

Implement both MSX and MSX2 in this worktree. They share common components, so fixes should be made in shared MSX/MSX2 infrastructure wherever the behavior is common.

The user's local C-BIOS root is:

```text
D:\emu\msx\bios
```

Important user correction: a blank Mnemos Player window is not emulator proof. Any player proof must explicitly pass both `--system msx|msx2` and `--rom <path>`.

## Current Verdict

MSX and MSX2 are not yet in a proven "100% working" state.

Proven so far:

- The Windows MSVC debug build passed before this handoff.
- Focused MSX/MSX2, TMS9918A, V9938, system-launch, and capability-summary tests passed before this handoff.
- Explicit player launches with real C-BIOS files and real cartridge ROMs run without process failure.
- C-BIOS logo ROM support is wired for MSX1 and MSX2.
- Bosconia reaches cartridge code after the logo ROM is mounted.
- VDP frame IRQs are delivered to the Z80; IRQ delivery is not the current blocker.

Not proven:

- Bosconia does not reach a playable display. The visible framebuffer remains the C-BIOS logo.
- MSX2 Bosconia also remains on the C-BIOS logo.
- `1Kpong!` shows C-BIOS/application output but reports `No enough memory`.
- No broad MSX/MSX2 ROM corpus is green.

## Local Firmware And ROMs

These paths were verified present on this machine during handoff:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
D:\emu\msx\MSX files [ROM]\Bosconia.rom
D:\emu\msx\MSX files [ROM]\1Kpong! (Phoenix Software) (2005) (Version del juego en color azul).rom
```

Bosconia observed cartridge facts:

- Plain 16 KiB ROM.
- Header bytes at file offset 0 start with `41 42`.
- Init vector is `$400F`.
- The ROM maps as a lower-page 16K cartridge.

## Validation Already Run

Build:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug'
```

Result: passed.

Focused MSX/MSX2 tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "msx|MSX|tms9918a|v9938" --output-on-failure'
```

Result: 10/10 passed.

Launch/capability tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_apps_player_system_launch_test|mnemos_apps_player_capability_summary_test" --output-on-failure'
```

Result: 2/2 passed.

No new build or ctest run was performed after the latest trace reduction. The next agent should rebuild after patching the isolated root cause.

## Explicit Player Proof Attempts

MSX1 Bosconia:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_REGION='ntsc'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system msx --rom 'D:\emu\msx\MSX files [ROM]\Bosconia.rom' --screenshot build\scratch\msx-explicit-proof\post-logo\bosconia-msx1-3600.png --frames 3600
```

Result: process exits 0, screenshot is still the C-BIOS logo. At 10000 frames it is still the C-BIOS logo.

MSX2 Bosconia:

```powershell
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_MSX2_REGION='ntsc'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system msx2 --rom 'D:\emu\msx\MSX files [ROM]\Bosconia.rom' --screenshot build\scratch\msx-explicit-proof\post-logo\bosconia-msx2-3600.png --frames 3600
```

Result: process exits 0, screenshot is still the C-BIOS logo.

MSX1 `1Kpong!`:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_REGION='ntsc'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system msx --rom 'D:\emu\msx\MSX files [ROM]\1Kpong! (Phoenix Software) (2005) (Version del juego en color azul).rom' --screenshot build\scratch\msx-explicit-proof\post-logo\1kpong-msx1-3600.png --frames 3600
```

Result: process exits 0, screenshot shows `No enough memory`.

## Current Diagnostic Evidence

The useful CPU trace for Bosconia is:

```text
build\scratch\msx-trace\bosconia-msx1-1200.png.cpu_trace.csv
```

Exact counts from that trace:

```text
00400F=8
004010=0
004128=528
004A1E=40
008010=9
0080AD=90
008AFB=8
lower_4000_7fff=114264
upper_8000_bfff=2443
```

First observed Bosconia lower-page cartridge entries:

```text
138,128728,00400F,8224739
138,128729,004012,8224749
138,128730,004015,8224759
138,128731,004018,8224769
```

Interpretation: Bosconia's init at `$400F` is reached. The failure is not just a missing cartridge visibility/header path.

Bosconia startup disassembly summary:

```text
400F LD HL,$6000
4012 LD DE,$8000
4015 LD BC,$2000
4018 LDIR              ; copies $6000-$7FFF to $8000-$9FFF
401A LD SP,$E800
401D CALL $0138
4020 RRCA
4021 RRCA
4022 AND $03
4036 CALL $0024        ; ENASLT
4041 CALL $0047        ; DISSCR loop
4049 CALL $8708        ; copied cartridge/RAM code
404C CALL $8910
404F CALL $491E
4052 clears E000-EFFF via LDIR
```

`$8AFB` in the trace is copied cartridge code from Bosconia ROM offset `$2AFB`, not the logo ROM. `cbios_logo_msx1.rom` has `FF` at the corresponding offsets. This means page 2 was RAM at that point, then later returns to slot 0/logo visibility.

Golden diagnostic command for MSX1 Bosconia:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_ROM='D:\emu\msx\MSX files [ROM]\Bosconia.rom'
$env:MNEMOS_MSX_REGION='ntsc'
$env:MNEMOS_MSX_BOOT_FRAMES='170'
$env:MNEMOS_MSX_BOOT_SHA256='0000000000000000000000000000000000000000000000000000000000000000'
$env:MNEMOS_MSX_PC_WATCH='4050-40D0'
Remove-Item Env:MNEMOS_MSX_D800_WATCH -ErrorAction SilentlyContinue
Remove-Item Env:MNEMOS_MSX_VDP_WATCH -ErrorAction SilentlyContinue
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe 'msx boots real firmware to a deterministic golden framebuffer' -s
```

Expected failure because the hash is intentionally wrong. Useful final state:

```text
frame 170 hash: 10c97eadcc1c32ef552dfa7c028c14f1667b782b1aa6ee478ab14ec325445a52
cpu pc/sp/af/bc/de/hl: $108B/$F2FE/$0048/$6B84/$19C4/$880C halted=true
slot state: primary=$F4 secondary0=$00 secondary1=$00 secondary2=$00 secondary3=$00
vdp state: frame=170 mode=3 r0=$02 r1=$E0 r2=$06 r7=$F5 vram_nonzero=5172 first_pixel=8222460
f390=[98,F3,08,F1,D3,A8,08,C9,DD,E9,...]
fd90=[00... C9 C9...]
```

Slot interpretation for `primary=$F4`:

- page 0: BIOS slot 0
- page 1: cartridge slot 1
- page 2: RAM slot 3
- page 3: RAM slot 3

Collapsed PC stream after the first `$400F` entry:

```text
138,128728,00400F,8224739,x1
138,128729,004012,8224749,x1
138,128730,004015,8224759,x1
138,128731,004018,8224769,x8192
141,136923,00401A,8396796,x1
141,136924,00401D,8396806,x1
141,136925,000138,8396823,x1
141,136926,00176A,8396833,x1
141,136928,004020,8396854,x1
141,136944,000024,8396972,x1
141,136997,000047,8397312,x1
141,137058,004044,8397913,x1
141,137061,004047,8397933,x1
141,137230,004049,8399348,x1
141,137231,008708,8399365,x1
141,137249,00870F,8399527,x1
141,137251,004719,8399551,x1
141,137252,00471B,8399558,x17
141,137269,00471D,8399774,x1
141,137270,00471E,8399778,x1
```

Key point: execution reaches copied cartridge/RAM code at `$8708`, then spends time around `$4719/$471B`. Later trace sections enter BIOS VRAM transfer loops around `$0278/$027A`, so VDP write paths are active but the visible logo remains.

C-BIOS XML layout was checked:

- MSX1: slot 0 main ROM `$0000-$7FFF`, slot 0 logo `$8000-$BFFF`, external slots 1/2, slot 3 full 64K RAM, TMS9918A.
- MSX2: slot 0 main `$0000-$7FFF`, slot 0 logo `$8000-$BFFF`, external slots 1/2, slot 3 subslot 0 sub-ROM, slot 3 subslot 2 512K memory mapper, V9938.

## Strong Current Hypotheses

This is not a blank-launch or missing-ROM-argument issue. It is also probably not a dead VDP IRQ line.

Most likely shared root areas:

- MSX BIOS work-area and slot variable behavior, especially around `EXPTBL`/`SLTTBL` and `CALL $0138`.
- `ENASLT`/PPI port `$A8` side effects and whether primary slot latch state is restored exactly as C-BIOS/game code expects.
- Cartridge takeover semantics after init returns or jumps through BIOS hooks/vectors.
- RAM discovery or usable-RAM bookkeeping; `1Kpong!` reporting `No enough memory` is an important clue.
- Z80 block instructions or BIOS VRAM helper behavior only if slot/RAM work-area checks do not explain the failure.

Lower-priority checks:

- Keyboard/input progression. This can be tested with `MNEMOS_MSX_BOOT_KEYS=space,return`, but it should not explain Bosconia staying on the logo after cartridge display setup begins.

## Recommended Resume Path

1. Inspect PPI helper semantics in `src/manifests/common/msx_io_ports.*`, especially `msx_ppi_port_a_output`.
2. Add or extend a focused diagnostic to dump MSX work-area addresses after Bosconia init and after the first return to BIOS wait code.
3. Trace the `$4719/$471B` loop and identify the condition it is waiting on.
4. Compare C-BIOS cartridge boot state with `tests/golden/README.md` profiles and `tests/golden/msx_rom_profiles.json`.
5. Run a small real-ROM smoke set to see whether Bosconia is isolated or representative.
6. Patch the shared MSX/MSX2 root cause, then add a regression test before rerunning validation.

Small smoke command template:

```powershell
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir build/windows-msvc-debug `
  -MsxBios 'D:\emu\msx\bios\cbios\cbios_main_msx1.rom' `
  -MsxLogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx1.rom' `
  -MsxRegion ntsc `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2ExpandedSlots 8 `
  -Msx2SubSlot 3.0 `
  -Msx2RamSlot 3.2 `
  -Msx2RamSize 512K `
  -Msx2Region ntsc `
  -MsxRomDir 'D:\emu\msx\MSX files [ROM]' `
  -Msx2RomDir 'D:\emu\msx\MSX files [ROM]' `
  -RomProfileManifest tests/golden/msx_rom_profiles.json `
  -Frames 3600 `
  -MaxRoms 5 `
  -RequireData
```

## Useful Files

- `src/manifests/msx/msx_system.cpp`
- `src/manifests/msx/msx_system.hpp`
- `src/manifests/msx2/msx2_system.cpp`
- `src/manifests/msx2/msx2_system.hpp`
- `src/manifests/common/msx_cartridge_mapper.cpp`
- `src/manifests/common/msx_cartridge_mapper.hpp`
- `src/manifests/common/msx_io_ports.cpp`
- `src/manifests/common/msx_io_ports.hpp`
- `src/apps/player/system_launch.cpp`
- `src/apps/player/adapters/msx/msx_adapter.cpp`
- `src/apps/player/adapters/msx2/msx2_adapter.cpp`
- `tests/golden/msx_boot_test.cpp`
- `tests/golden/README.md`
- `tests/golden/msx_rom_profiles.json`
- `scripts/msx/run-boot-smoke.ps1`
- `src/chips/cpu/z80/z80.cpp`
- `src/chips/video/tms9918a/tms9918a.cpp`

## Git Notes

Before this handoff update, `git status --short --branch` was clean:

```text
## feature/msx2...origin/feature/msx2
```

This file is the handoff artifact requested by the user before switching agents due to the long-running session context size.
