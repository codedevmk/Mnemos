# MSX / MSX2 Resume Handoff

Generated: 2026-06-27 00:20 America/Chicago
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

The root checkout at `C:\dev\emu\Mnemos` is not the active MSX/MSX2 worktree.
Continue MSX/MSX2 work from `C:\dev\emu\Mnemos-msx2`.

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
D:\emu\msx\bios\cbios\cbios_main_msx2_eu.rom
D:\emu\msx\bios\cbios\cbios_main_msx2_jp.rom
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
- `abbaye_v1.1.rom` now resolves its strong ASCII8 loader-write signature
  before a lower-page self-modifying-code hit can misclassify it as Generic8.
- The MSX/MSX2 smoke runner now treats empty/zero-byte logs as "no hash" rather
  than throwing from the regex parser; this was hit by a profile-backed
  baseline probe during the skip-132 window.
- Bounded real-ROM smoke windows have passed through skip 191:
  - `-SkipRoms 12 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 24 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 36 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 48 -MaxRoms 12`: `26/26` passed after profiling
    `a_test2 [Arabic MSX].rom` against BR C-BIOS.
  - `-SkipRoms 60 -MaxRoms 12`: `26/26` passed after fixing the shared
    ASCII8-vs-Generic8 mapper classifier.
  - `-SkipRoms 72 -MaxRoms 12`: `25/25` passed; `AKUPRO.rom` is skipped on
    MSX by profile as MSX2-only and validated on MSX2.
  - `-SkipRoms 84 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 96 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 108 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 120 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 132 -MaxRoms 12`: `25/25` passed after the runner parser fix;
    `ASHGUINZ.rom` is skipped on MSX by profile as MSX2-only and validated on
    MSX2.
  - `-SkipRoms 144 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 156 -MaxRoms 12`: `26/26` passed; `b_game` retried on MSX2 at
    3600 frames and passed.
  - `-SkipRoms 168 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 180 -MaxRoms 12`: `25/25` passed;
    `barbarian_the_duel_v1.0-msxdev[ASCII16].rom` is skipped on MSX by profile
    as MSX2-only and validated on MSX2.

Known gaps:

- The current active blocker is the skip-192 corpus window: `bean.rom` passes
  on MSX, but fails on MSX2 because the framebuffer remains uniform after retry.
- This is not yet a representative compatibility matrix.
- Earlier notes included Bosconia staying on the C-BIOS logo and MSX2 Bestial
  Warrior color fidelity suspicion; those still need confirmation in later
  slices.

## Current Branch State

Before this handoff update, the branch was clean and pushed at:

```text
788cbece (HEAD -> feature/msx2, origin/feature/msx2) Update MSX2 resume handoff
```

Recent commits:

```text
788cbece Update MSX2 resume handoff
af64b5c2 Advance MSX2 corpus to skip 156
f02e37ba Harden MSX boot smoke hash parsing
bff97edd Advance MSX2 corpus to skip 108
4859188e Advance MSX2 corpus to skip 96
691ccca9 Advance MSX2 corpus handoff
631c4fb9 Fix MSX ASCII8 mapper priority
65336c1f Refresh MSX2 resume handoff
```

## Latest Validation: Skip 192 Window

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
  -SkipRoms 192 `
  -MaxRoms 12 `
  -RequireData
```

Result:

```text
MSX/MSX2 boot smoke: 25/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-235230-882-50236\summary.json
failure: msx2/rom-bean exit=42
log: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-235230-882-50236\019-msx2-rom-bean.log
retry log: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-235230-882-50236\019-msx2-rom-bean-retry-3600.log
```

Hashes:

```text
MSX bean.rom:
690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a

MSX2 bean.rom:
9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573
```

The MSX2 hash differs from the firmware baseline, but Catch2 failed because
the framebuffer was uniform:

```text
CHECK_FALSE(framebuffer_is_uniform(fb)) failed
frames rendered: 3600
resolved cartridge mapper: Plain
slot layout: expanded=8 ram=3.2 sub=3.0 disk=default cart2=default ram_size=524288
CPU pc/sp/af/bc/de/hl: $CA3E/$E6FF/$5FBA/$0101/$5F01/$66B8 halted=true
cycles=215049602
slot state: primary=$D0 secondary0=$00 secondary1=$00 secondary2=$00 secondary3=$A0
RAM mapper segments: [3,2,1,0]
VDP: frame=3600 mode=4 r0=$02 r1=$E2 r2=$06 r7=$F4 vram_nonzero=6104
V9938 extended: r8=$08 r9=$00 r3=$FF r4=$03 r5=$36 r6=$07 r10=$00 r11=$00 r18=$00 r13=$00 r23=$00 r44=$00 r45=$00 r46=$08 s2=$0C
```

Important interpretation:

- `mode=4` is the emulator enum value for `graphics_ii`; it is not MSX2
  Graphics 4.
- The test's `visible_g4_*` diagnostics are generic probes and not proof that
  Graphics 4 is active.
- The current evidence points at a CPU/VDP interrupt or HALT wake issue in the
  MSX2/V9938 path, not an invalid ROM and not a profile skip candidate.

## Bean.rom Diagnostics

ROM:

```text
D:\emu\msx\MSX files [ROM]\bean.rom
Size: 16384 bytes
Header starts at offset 0: 41 42 04 80 ...
Init vector: $8004
Mapper: Plain
```

MSX1 forced-diagnostics command:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_ROM='D:\emu\msx\MSX files [ROM]\bean.rom'
$env:MNEMOS_MSX_REGION='ntsc'
$env:MNEMOS_MSX_BOOT_FRAMES='600'
$env:MNEMOS_MSX_BOOT_SHA256='force-diagnostics'
& '.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe' '[golden][msx]' 2>&1 |
  Tee-Object -FilePath 'build\scratch\msx-boot\bean-msx-diagnostics.log'
```

MSX1 result:

```text
framebuffer nonuniform
hash=690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a
CPU pc/sp/af/bc/de/hl: $829D/$E395/$A68C/$0000/$0008/$9C56 halted=false
slot state: primary=$D0 secondary0=$00 secondary1=$00 secondary2=$00 secondary3=$00
VDP: frame=600 mode=3 r0=$02 r1=$E2 r2=$06 r7=$04 vram_nonzero=7336 first_pixel=5527021
```

This proves the same ROM keeps running and displays on MSX1, while MSX2 reaches
a HALT in RAM and never wakes to produce nonuniform output.

Variants already tried:

- RAM/slot variants: 512K, 64K, 128K, plain64, and a slot3_0_ram variant. The
  useful variants still halted at `$CA3E` with the same VDP state.
- C-BIOS regional variants: `cbios_main_msx2.rom`, `_br`, `_eu`, and `_jp`.
  All failed the same; JP changed only backdrop register `r7` from `$F4` to
  `$F7`.
- Boot keys: none, `1`, `2`, `shift`, `ctrl`, `space`. Shift/ctrl/space parsed
  and still failed; `1` and `2` did not map through the parser and printed
  `none`.

CPU trace command:

```powershell
$out='build\scratch\msx-bean-trace\bean-msx2.ppm'
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_CPU_TRACE='1'
& '.\build\windows-msvc-debug\src\apps\player\mnemos_player.exe' `
  --system msx2 `
  --rom 'D:\emu\msx\MSX files [ROM]\bean.rom' `
  --screenshot $out `
  --frames 600 2>&1 |
  Tee-Object -FilePath 'build\scratch\msx-bean-trace\player-msx2.log'
```

Trace artifacts:

```text
build\scratch\msx-bean-trace\bean-msx2.ppm
build\scratch\msx-bean-trace\bean-msx2.ppm.cpu_trace.csv
PPM SHA256: 5202D0AB1B6E98088819151488F5D6347AC94CA1380EBCF5A086269A27889A74
```

Trace tail:

```text
245,459034,008294,14621505
245,459035,008295,14621516
245,459036,008296,14621527
245,459037,008299,14621537
245,459038,00829A,14621548
245,459039,008282,14621558
245,459040,008283,14621568
245,459041,008284,14621578
245,459042,00CA29,14621592
...
245,459061,00CA3C,14621711
245,459062,00CA3D,14621718
```

The trace stops after `$CA3D`; the retry log reports `halted=true` at `$CA3E`.
This strongly suggests the ROM uses HALT plus VDP interrupt timing and the
MSX2/V9938 path is not waking or accepting the interrupt correctly.

## Code Pointers

Read these first for the skip-192 failure:

```text
src/chips/video/tms9918a.cpp
src/chips/video/tms9918a.hpp
src/chips/video/v9938.cpp
src/chips/video/v9938.hpp
src/chips/cpu/z80/z80.cpp
src/chips/cpu/z80/z80.hpp
src/manifests/msx/msx_system.cpp
src/manifests/msx2/msx2_system.cpp
src/manifests/msx2/tests/msx2_system_test.cpp
tests/golden/msx_boot_test.cpp
```

Known wiring:

- `src/manifests/msx/msx_system.cpp` wires both `vdp` and `vdp2` IRQ callbacks
  to `s->cpu.set_irq_line(asserted)`.
- `src/manifests/msx2/msx2_system.cpp` wires V9938 IRQ callback to
  `s->cpu.set_irq_line(asserted)`.
- So the likely issue is not a missing callback. Inspect V9938 status/IRQ clear
  semantics, status register selection via R15, frame IRQ enable handling, and
  Z80 level IRQ/HALT wake behavior.

Focused hypothesis:

- V9938 sets `status_[0]` frame interrupt and updates IRQ at frame boundary.
- V9938 status reads are selected through register 15.
- The failing diagnostic shows `s2=$0C`; determine whether C-BIOS or the ROM is
  reading S2 while the frame IRQ flag remains in S0, and whether the emulator
  clears or preserves the IRQ line exactly like real V9938.
- A held level IRQ should still wake HALT only when the Z80 IFF state and IM
  behavior permit it. Verify Z80 interrupt acceptance and HALT exit timing
  against existing Z80 tests.

## Suggested Next Implementation Slice

1. Add or find a focused hermetic regression for MSX2/V9938-to-Z80 interrupts.
   A useful regression should drive a simple EI/HALT loop with VDP frame IRQ
   enabled and verify that the CPU exits HALT/takes the interrupt.
2. Compare TMS9918A and V9938 frame interrupt status behavior and status-read
   clearing logic.
3. Fix the V9938 IRQ/status or Z80 HALT/IRQ behavior at the root cause.
4. Rebuild focused targets and run focused tests.
5. Re-run `bean.rom` on both MSX and MSX2, then re-run the skip-192 corpus
   window.
6. If skip 192 passes, update this file with next window
   `-SkipRoms 204 -MaxRoms 12`.

Build and focused-test command shape:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test mnemos_manifests_msx2_test mnemos_manifests_msx_test'
```

Targeted validation after a fix should use the same C-BIOS args and either
single-ROM `bean.rom` probes or the full skip-192 command above:

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

## Do Not Do

- Do not profile-skip `bean.rom` unless there is hard evidence that the local
  dump is invalid or MSX1-only. Current evidence points to a real emulation
  gap.
- Do not treat a player window without `--system` and `--rom` as validation.
- Do not commit files from `build\`, firmware dumps, ROM corpus files,
  screenshots, CPU traces, or logs.
- Do not reuse the earlier broken PowerShell pixel-uniqueness snippet; it
  produced excessive error spam/timeouts. If image uniqueness is needed, write
  a small bounded parser under `build\scratch\` or use existing test helpers.

