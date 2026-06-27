# MSX / MSX2 Resume Handoff

Generated: 2026-06-27 00:19 America/Chicago
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
- C-BIOS lives under `D:\emu\msx\bios`.
- Preserve the requested branch/worktree name: `feature/msx2`.
- Keep ROMs, firmware, screenshots, logs, and build outputs out of Git.
- Put transient artifacts under `build\`, preferably `build\scratch\...`.

## BIOS / ROM Inputs

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

Do not commit any of those input files.

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
  than throwing from the regex parser.
- Bounded real-ROM smoke windows have passed through skip 191. The most recent
  fully passing window was `-SkipRoms 180 -MaxRoms 12`, with `25/25` passing.

Known gaps:

- The current active blocker is the skip-192 corpus window: `bean.rom` passes
  on MSX, but fails on MSX2 because the framebuffer remains uniform after retry.
- This is not yet a representative compatibility matrix.
- Earlier notes included Bosconia staying on the C-BIOS logo and MSX2 Bestial
  Warrior color fidelity suspicion; those still need confirmation in later
  slices.

## Branch State At Handoff

Before the handoff commit, the branch was pushed at:

```text
c9674c71 (HEAD -> feature/msx2, origin/feature/msx2) Update MSX2 resume handoff
```

The handoff commit includes:

- `RESUME.md` refreshed with this current state.
- `src/manifests/msx2/tests/msx2_system_test.cpp` with a new regression proving
  a V9938 frame IRQ wakes a HALTed Z80 through the MSX2 system IRQ path.
- `tests/golden/msx_boot_test.cpp` with additional CPU and VDP diagnostics for
  MSX/MSX2 firmware failures.

## Latest Validation

Focused build:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test'
```

Result: passed.

Focused tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test" --output-on-failure'

cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_msx_boot_test" --output-on-failure'
```

Results:

```text
1/1 Test #134: mnemos_manifests_msx2_test ... Passed
1/1 Test #174: mnemos_msx_boot_test ........ Passed
```

No full `ctest` sweep was run after the latest diagnostic-only test edits.

## Skip 192 Blocker

Command that exposed the active blocker:

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
failure: msx2/rom-bean exit=42
```

The logs from that run were under:

```text
C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-235230-882-50236\
```

Hashes:

```text
MSX bean.rom:
690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a

MSX2 bean.rom:
9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573
```

The MSX2 hash differs from the firmware baseline, but Catch2 failed because
the framebuffer was uniform after 3600 frames.

## Bean.rom Diagnostics

ROM:

```text
D:\emu\msx\MSX files [ROM]\bean.rom
Size: 16384 bytes
Header starts at offset 0: 41 42 04 80 ...
Init vector: $8004
Mapper: Plain
```

Latest MSX2 forced-diagnostics command:

```powershell
New-Item -ItemType Directory -Force -Path 'build\scratch\msx-bean-diagnostics' | Out-Null
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_ROM='D:\emu\msx\MSX files [ROM]\bean.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_MSX2_REGION='ntsc'
$env:MNEMOS_MSX2_BOOT_FRAMES='3600'
$env:MNEMOS_MSX2_BOOT_SHA256='force-diagnostics'
& '.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe' '[golden][msx2]' 2>&1 |
  Tee-Object -FilePath 'build\scratch\msx-bean-diagnostics\bean-msx2-3600-diagnostics.log'
```

Expected exit is 1 because the SHA is deliberately forced to mismatch and the
framebuffer is uniform.

Key output:

```text
CPU pc/sp/af/bc/de/hl: $CA3E/$E6FF/$5FBA/$0101/$5F01/$66B8 halted=true iff1=false iff2=false im=0 cycles=215049602
VDP: frame=3600 mode=4 r0=$02 r1=$E2 r2=$06 r7=$F4 r15=$00 s0=$C4 s1=$01 irq=true vram_nonzero=6104 first_pixel=2368548
V9938 extended: r8=$08 r9=$00 r3=$FF r4=$03 r5=$36 r6=$07 r10=$00 r11=$00 r18=$00 r13=$00 r23=$00 r44=$00 r45=$00 r46=$08 s2=$0C
```

Interpretation:

- The V9938 IRQ line is asserted.
- Status register 0 has the frame IRQ bit set.
- VDP register 15 is 0, so status reads are not stuck on S#2 in this run.
- The CPU is halted with both IFF flags false and IM 0.
- This is not simply "VDP never interrupts." The ROM reaches a final HALT with
  interrupts disabled or after an interrupt path fails to re-enable them.

## PC Watch Evidence

Run with:

```powershell
$env:MNEMOS_MSX2_BOOT_FRAMES='600'
$env:MNEMOS_MSX_PC_WATCH='$CA00-$CA80'
& '.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe' '[golden][msx2]' 2>&1 |
  Tee-Object -FilePath 'build\scratch\msx-bean-diagnostics\bean-msx2-pcwatch-ca00.log'
```

Observed sequence:

- The BIOS trampoline switches slots around `$F380`; previous PC was `$23FC`.
- The CPU later enters `$CA00` from `$C9FF`, executing zero/data-like RAM.
- At `$CA28` (`HALT`), a frame interrupt is accepted and vectors to `$0038`.
- The interrupt path reaches `$8284`, which contains `ED 45` (`RETN`), then
  returns to `$CA29`.
- Execution continues through RAM data/NOPs to final `$CA3D=$76`; final PC is
  `$CA3E` with `IFF1=false`.

A broader watch used:

```powershell
$env:MNEMOS_MSX_PC_WATCH='$C800-$CA50'
& '.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe' '[golden][msx2]' 2>&1 |
  Tee-Object -FilePath 'build\scratch\msx-bean-diagnostics\bean-msx2-pcwatch-c800-ca50.log'
```

Key first transition:

```text
range previous_pc=$C7FF current_pc=$C800 cycles=14574251 ... sp=$E5AF
```

The output is large and stored in `build\scratch`; inspect it locally if needed.

## New Regression Added

`src/manifests/msx2/tests/msx2_system_test.cpp` now contains:

```text
TEST_CASE("msx2 VDP frame IRQ wakes the Z80 from HALT", "[manifests][msx2][irq]")
```

The test:

- Builds a zero-filled 32KB BIOS with `IM 1`, `EI`, `HALT`.
- Enables VDP display and frame IRQ through R#1.
- Ticks V9938 through one visible frame interval.
- Asserts VDP IRQ is asserted.
- Steps the CPU and verifies it exits HALT, vectors to `$0038`, and clears IFF1.

This regression passes, so the generic MSX2 VDP-to-Z80 IRQ wire is not missing.

## Diagnostic Fields Added

`tests/golden/msx_boot_test.cpp` now reports:

- MSX and MSX2 CPU `iff1`, `iff2`, and `im`.
- MSX2 VDP `r15`, `s0`, `s1`, and `irq`.

These are diagnostics only; they do not change golden expectations.

## High-Value Hypotheses

Investigate these next:

1. Confirm Z80 maskable interrupt handling for IFF2. Current code clears both
   `iff1_` and `iff2_` on maskable IRQ, while `RETN/RETI` restores `iff1_` from
   `iff2_`. If real Z80 maskable INT must preserve IFF2, this would explain
   why the BIOS `RETN` path returns with interrupts disabled.
2. If IFF2 clearing is correct, trace why PC falls through into `$C800+` RAM.
   The useful clue is the transition from `$C7FF` to `$C800`, followed by
   execution of zero/data-like RAM.
3. Compare an MSX1 PC watch for `bean.rom` over `$C800-$CA50` to identify
   whether MSX1 avoids this high-RAM fallthrough or recovers differently.
4. If touching CPU interrupt semantics, add focused Z80 regression coverage and
   run `mnemos_chips_cpu_z80_test` before broader MSX/MSX2 validation.

Important code locations:

```text
src/chips/cpu/z80/z80.cpp
src/chips/cpu/z80/tests/z80_test.cpp
src/chips/video/v9938/v9938.cpp
src/manifests/msx2/tests/msx2_system_test.cpp
tests/golden/msx_boot_test.cpp
```

## Suggested Next Commands

If starting from a clean checkout of this branch:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_cpu_z80_test mnemos_manifests_msx2_test mnemos_msx_boot_test'

cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_chips_cpu_z80_test|mnemos_manifests_msx2_test|mnemos_msx_boot_test" --output-on-failure'
```

To reproduce the active blocker, rerun the skip-192 smoke window from the
section above.

To compare MSX1 high-RAM behavior:

```powershell
New-Item -ItemType Directory -Force -Path 'build\scratch\msx-bean-diagnostics' | Out-Null
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_ROM='D:\emu\msx\MSX files [ROM]\bean.rom'
$env:MNEMOS_MSX_REGION='ntsc'
$env:MNEMOS_MSX_BOOT_FRAMES='600'
$env:MNEMOS_MSX_BOOT_SHA256='force-diagnostics'
$env:MNEMOS_MSX_PC_WATCH='$C800-$CA50'
& '.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe' '[golden][msx]' 2>&1 |
  Tee-Object -FilePath 'build\scratch\msx-bean-diagnostics\bean-msx-pcwatch-c800-ca50.log'
```

## What Not To Do

- Do not mark `bean.rom` as a profile skip. It passes on MSX and exposes a real
  MSX2 execution/interrupt/state issue.
- Do not treat the VDP IRQ line as unimplemented. The new regression proves the
  generic line can wake a HALTed Z80.
- Do not claim the MSX2 renderer is the root cause until CPU execution reaches a
  sane game loop with nonzero visible name/pattern/color table usage.
- Do not commit ROMs, BIOS files, screenshots, logs, or `build\` contents.
