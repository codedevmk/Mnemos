# MSX / MSX2 Handoff

Date: 2026-06-25
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`

## User Goal

Implement both MSX and MSX2 in this worktree because they share common components.
The user supplied C-BIOS firmware location:

```text
D:\emu\msx\bios
```

Important user correction: do not treat a blank Mnemos Player window as an
emulator proof. A valid player launch must pass both `--system` and `--rom`.

## Current Verdict

MSX and MSX2 are not yet in a proven "100% working" state.

What is proven:

- The code builds with the Windows MSVC debug preset.
- Focused MSX/MSX2, TMS9918A, V9938, system-launch, and capability-summary tests pass.
- Explicit player launches with real C-BIOS files and a real cartridge ROM run without process failure.
- C-BIOS logo ROM support is now wired for MSX1 and MSX2.
- Bosconia reaches cartridge code after the logo ROM is mounted.
- VDP frame IRQs are delivered to the Z80; the CPU is accepting interrupts through `$0038`.

What is not proven:

- Bosconia does not reach a playable display. It remains visually on the C-BIOS logo framebuffer while the CPU parks in a BIOS frame-delay/HALT path.
- A second tested MSX1 ROM (`1Kpong!`) reaches C-BIOS/application output but reports `No enough memory`.
- No broad ROM corpus is green.

## Major Implemented Areas

- Shared MSX/MSX2 player and launch plumbing.
- MSX and MSX2 adapter registrations and capability summaries.
- MSX keyboard matrix and mouse input helpers.
- MSX cartridge mapper helpers, common I/O-port helpers, and tests.
- MSX Kanji ROM peripheral.
- MSX cassette, WD1793 disk, SSG, TMS9918A, and V9938 slices.
- MSX/MSX2 manifests, slot layout, cartridge handoff, RAM mapper, disk, RTC, SCC,
  MSX-MUSIC, cassette, logo ROM, and save-state surfaces.
- Real-firmware golden boot harness for MSX/MSX2.
- `scripts/msx/run-boot-smoke.ps1` for BIOS/profile/manifest driven smoke runs.

## Important Recent Patch State

MSX1 logo ROM support:

- `src/manifests/msx/msx_system.hpp`
  - Added `msx_config::logo_rom`.
  - Added `msx_system::logo_rom`.
- `src/manifests/msx/msx_system.cpp`
  - BIOS slot page 2 now reads optional logo ROM at `$8000-$BFFF`.
  - `assemble_msx` copies `config.logo_rom`.
- `src/apps/player/adapters/msx/msx_adapter.cpp`
  - Publishes `logo_rom` media.
  - Adds spec field `Logo ROM = slot 0 $8000-$BFFF`.
  - Registry reads `opts.bios_images[2]`.
- `src/apps/player/system_launch.cpp`
  - Loads `MNEMOS_MSX_LOGO_ROM` into BIOS image slot 2.
  - Fixed MSX2 Kanji insertion to preserve stable logo ROM slot indexing.
- `scripts/msx/run-boot-smoke.ps1`
  - Added `MNEMOS_MSX_LOGO_ROM` handling.
- `tests/golden/msx_boot_test.cpp`
  - Reads optional `MNEMOS_MSX_LOGO_ROM` for MSX1.

MSX2 logo ROM support already exists and uses:

- `MNEMOS_MSX2_LOGO_ROM`
- `MNEMOS_MSX2_SUB_ROM`
- expanded slot/RAM profile environment variables.

## Local Firmware And ROMs Used

Known existing local files:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
D:\emu\msx\MSX files [ROM]\Bosconia.rom
D:\emu\msx\MSX files [ROM]\1Kpong! (Phoenix Software) (2005) (Version del juego en color azul).rom
```

Bosconia size observed: 16 KiB.

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

## Explicit Player Proof Commands

MSX1 Bosconia:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_REGION='ntsc'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system msx --rom 'D:\emu\msx\MSX files [ROM]\Bosconia.rom' --screenshot build\scratch\msx-explicit-proof\post-logo\bosconia-msx1-3600.png --frames 3600
```

Result: process exits 0, screenshot is the C-BIOS logo. At 10000 frames it is
still the C-BIOS logo.

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

Result: process exits 0, screenshot is the C-BIOS logo.

MSX1 1Kpong:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_REGION='ntsc'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system msx --rom 'D:\emu\msx\MSX files [ROM]\1Kpong! (Phoenix Software) (2005) (Version del juego en color azul).rom' --screenshot build\scratch\msx-explicit-proof\post-logo\1kpong-msx1-3600.png --frames 3600
```

Result: process exits 0, screenshot shows `No enough memory`.

## Golden Diagnostic Evidence

MSX1 Bosconia diagnostic command:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_ROM='D:\emu\msx\MSX files [ROM]\Bosconia.rom'
$env:MNEMOS_MSX_REGION='ntsc'
$env:MNEMOS_MSX_BOOT_FRAMES='3600'
$env:MNEMOS_MSX_BOOT_SHA256='0000000000000000000000000000000000000000000000000000000000000000'
build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe 'msx boots real firmware to a deterministic golden framebuffer' -s
```

Expected failure because the hash is intentionally wrong. Useful output:

```text
hash: 10c97eadcc1c32ef552dfa7c028c14f1667b782b1aa6ee478ab14ec325445a52
cpu pc/sp/af/bc/de/hl: $108B/$F2FE/$0048/$6D84/$19C4/$880C halted=true
cycles=215049601
slot state: primary=$F4 secondary0=$00 secondary1=$00 secondary2=$00 secondary3=$00
ram state: pages16k=[0,0,7600,1007]
vdp state: frame=3600 mode=3 r0=$02 r1=$E0 r2=$06 r7=$F5 vram_nonzero=5172 first_pixel=8222460
```

Interpretation:

- Slot state is plausible after cartridge handoff:
  - page 0: BIOS slot 0
  - page 1: cartridge slot 1
  - pages 2 and 3: RAM slot 3
- CPU is halted at BIOS code around `$108A/$108B`.
- The framebuffer hash is deterministic but still the logo.

PC watch around IRQ vector:

```powershell
$env:MNEMOS_MSX_BOOT_FRAMES='900'
$env:MNEMOS_MSX_PC_WATCH='0038-0120'
```

Useful result:

- CPU repeatedly transitions from `$108A` to `$0038`, then to `$18E6`.
- Return address on stack is `$108B`.
- This means frame IRQ delivery is working and wakes the CPU from HALT.
- The BIOS frame-delay counter changes across interrupts.

## Current Failure Signature

With MSX1 C-BIOS main + logo ROM and Bosconia:

- Cartridge code is reached after mounting the logo ROM.
- The machine ends up in a BIOS frame delay/wait routine:
  - PC window includes `$108A=$76` (`HALT`), `$108B=$10`, `$108C=$FD`.
  - Interrupt vector `$0038` jumps to `$18E6`.
- Interrupts are not dead. The CPU accepts frame interrupts.
- The display remains the C-BIOS logo framebuffer.

This suggests the next shared root-cause search should focus on game/BIOS
handoff state after the cartridge init path, not blank-launch plumbing and not a
missing VDP IRQ line.

## Recommended Next Investigation

Start here:

1. Compare C-BIOS cartridge boot path against expected slot/RAM work-area state.
2. Trace Bosconia's cartridge entry and return path after it reaches addresses around `$4128`, `$4A1E`, and `$8AFB`.
3. Inspect why the game leaves the logo VRAM intact instead of installing its own display.
4. Check whether C-BIOS marks usable RAM too low or wrong for 16 KiB/32 KiB cartridges.
5. Add a focused regression once the cause is isolated, then rerun:
   - MSX/MSX2 focused tests
   - system launch tests
   - explicit player screenshot launches
   - smoke script with MSX and MSX2 profiles

Candidate areas from current evidence:

- RAM discovery/work-area initialization.
- Cartridge header/init dispatch and return semantics.
- Z80 block I/O correctness, especially BIOS VRAM routines using `INI/OUTI/INIR/OTIR`.
- BIOS slot switching helper state around PPI port `$A8`.
- VDP register/status side effects after BIOS logo and before cartridge display setup.

## Useful Files

- `src/manifests/msx/msx_system.cpp`
- `src/manifests/msx/msx_system.hpp`
- `src/manifests/msx2/msx2_system.cpp`
- `src/manifests/msx2/msx2_system.hpp`
- `src/apps/player/system_launch.cpp`
- `src/apps/player/adapters/msx/msx_adapter.cpp`
- `src/apps/player/adapters/msx2/msx2_adapter.cpp`
- `tests/golden/msx_boot_test.cpp`
- `tests/golden/README.md`
- `scripts/msx/run-boot-smoke.ps1`
- `src/chips/cpu/z80/z80.cpp`
- `src/chips/video/tms9918a/tms9918a.cpp`
- `src/chips/video/v9938/v9938.cpp`

## Build And Test Entry Points

Use this exact Visual Studio developer environment wrapping on Windows:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug'
```

Focused test sweep:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "msx|MSX|tms9918a|v9938" --output-on-failure'
```

Launch/capability tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_apps_player_system_launch_test|mnemos_apps_player_capability_summary_test" --output-on-failure'
```

## Do Not Overstate Completion

The correct continuation stance is:

- MSX/MSX2 implementation is materially advanced.
- Real firmware and ROM launch paths exist.
- The current branch is not yet compatibility-complete.
- The next agent should prove a playable ROM state with explicit `--system` and
  `--rom` launches before calling the slice complete.
