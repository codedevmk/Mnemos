# MSX / MSX2 Resume Handoff

Generated: 2026-06-27 00:28:43 -05:00 America/Chicago
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

Before this handoff update, the branch was clean and pushed at:

```text
523051c3 (HEAD -> feature/msx2, origin/feature/msx2) Update MSX2 resume handoff
```

The preceding pushed commit already included:

- `src/manifests/msx2/tests/msx2_system_test.cpp` with a regression proving a
  V9938 frame IRQ wakes a HALTed Z80 through the MSX2 system IRQ path.
- `tests/golden/msx_boot_test.cpp` with additional CPU and VDP diagnostics for
  MSX/MSX2 firmware failures.
- `RESUME.md` with the prior checkpoint.

This handoff commit updates `RESUME.md` only.

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

MSX1 600-frame forced-diagnostics run:

```text
Log:
build\scratch\msx-bean-diagnostics\bean-msx-pcwatch-c000-ca80.log

CPU pc/sp/af/bc/de/hl: $829D/$E395/$A68C/$0000/$0008/$9C56 halted=false iff1=true iff2=true im=0 cycles=35841613
vdp state: frame=600 mode=3 r0=$02 r1=$E2 r2=$06 r7=$04 vram_nonzero=7336 first_pixel=5527021
Hash:
690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a
```

MSX2 600-frame forced-diagnostics run:

```text
Log:
build\scratch\msx-bean-diagnostics\bean-msx2-pcwatch-c000-ca80.log

CPU pc/sp/af/bc/de/hl: $CA3E/$E6FF/$5FBA/$0101/$5F01/$66B8 halted=true iff1=false iff2=false im=0 cycles=35841602
slot state: primary=$D0 secondary0=$00 secondary1=$00 secondary2=$00 secondary3=$A0
ram mapper segments: [3,2,1,0]
vdp state: frame=600 mode=4 r0=$02 r1=$E2 r2=$06 r7=$F4 r15=$00 s0=$C4 s1=$01 irq=true vram_nonzero=6104 first_pixel=2368548
v9938 extended: r8=$08 r9=$00 r3=$FF r4=$03 r5=$36 r6=$07 r10=$00 r11=$00 r18=$00 r13=$00 r23=$00 r44=$00 r45=$00 r46=$08 s2=$0C
```

Interpretation:

- `bean.rom` does not enter `$C000-$CA80` on MSX1 in the 600-frame window.
- On MSX2 it reaches `$CA3E`, HALTs, and leaves interrupts disabled.
- `S2=$0C` has only the fixed status-2 bits set, so the V9938 command engine
  is not obviously stuck in an active command from this run.
- Changing MSX2 RAM size from `512K` to `64K` did not avoid the bad path.

Narrow PC watch:

```text
Log:
build\scratch\msx-bean-diagnostics\bean-msx2-pcwatch-bfe0-c020.log

range previous_pc=$99E3 current_pc=$BFFF cycles=14564941 ... sp=$E3AF ret0=$99E6
prev_code=[$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11,$99DF=$40,$99E0=$00,$99E1=$06,$99E2=$06,$99E3=$CD,$99E4=$FF,$99E5=$BF,$99E6=$21,...]
code=[$BFF7=$01,$BFF8=$08,$BFF9=$00,$BFFA=$09,$BFFB=$C1,$BFFC=$10,$BFFD=$E1,$BFFE=$C9,$BFFF=$C5,$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00]
```

Interpretation:

- The CPU did not accidentally fall through from `$BFFF`.
- ROM code deliberately executed `CALL $BFFF` at `$99E3`.
- `$BFFF` is the final byte of the 16 KiB ROM and contains `C5` (`PUSH BC`).
- Execution then continues into `$C000` RAM bytes and eventually reaches the
  bad HALT at `$CA3E`.

MSX2 watch around the selecting code:

```text
Log:
build\scratch\msx-bean-diagnostics\bean-msx2-pcwatch-9900-9a10.log

range previous_pc=$832D current_pc=$99DB cycles=14564897 ... hl=$99DB ix=$99CF iy=$0184 sp=$E3B1 ret0=$831C
prev_code=[$8325=$DD,$8326=$66,$8327=$01,$8328=$E5,$8329=$DD,$832A=$E1,$832B=$18,$832C=$CF,$832D=$E9,...]
```

Interpretation:

- `$832D` is `JP (HL)`, with `HL=$99DB`.
- The game deliberately indirect-jumps to `$99DB`.
- `$99DB` begins:

```text
$99DB: 21 FD 99 11 40 00 06 06 CD FF BF ...
```

That disassembles as:

```text
LD HL,$99FD
LD DE,$0040
LD B,$06
CALL $BFFF
```

## Rejected Hypothesis

Do not change Z80 IFF2 interrupt semantics for this blocker.

The current Z80 implementation accepts maskable interrupts by clearing both
`IFF1` and `IFF2`, and restores `IFF1` from `IFF2` on `RETN/RETI`. That looked
suspicious while diagnosing the HALT, but it is correct for Z80 maskable IRQ
acceptance. The `bean.rom` MSX2 failure is not an IFF2-preservation bug.

Useful source locations:

```text
src/chips/cpu/z80/z80.cpp
  RETN/RETI: restores IFF1 from IFF2.
  NMI: saves IFF1 into IFF2, then clears IFF1.
  Maskable IRQ acceptance: clears both IFF1 and IFF2.

src/chips/cpu/z80/tests/z80_test.cpp
  Existing interrupt tests check PC behavior, but not IFF2 semantics deeply.
```

## Current Working Hypothesis

The failing path is selected intentionally by the ROM on MSX2:

- Code around `$82F8-$832D` uses `IN A,($99)`, stores the VDP status byte to
  `$E3C5`, walks a table via `IX`, then reaches `$832D: JP (HL)`.
- On the failing run `HL=$99DB`, which is the routine that calls `$BFFF` and
  flows into `$C000`.
- MSX1 avoids `$99DB` entirely in the same 600-frame diagnostics.
- The likely root cause is earlier than the cartridge mirror itself: either
  the emulator exposes wrong V9938/MSX2 status during the ROM probe, or an MSX2
  slot/BIOS state difference makes the game choose an invalid table entry.

Important caution:

- Do not fix this by mirroring 16 KiB plain ROMs into page 3. Existing MSX/MSX2
  plain ROM tests intentionally keep lower-entry 16 KiB ROMs out of `$C000`.
  `bean.rom` is calling `$BFFF` as part of a selected MSX2 routine; the next
  investigation should find why that routine/table is selected.

## Recommended Next Step

Instrument VDP port reads/writes around the selecting path and compare MSX1
against MSX2.

The most direct route is a gated diagnostic in `tests/golden/msx_boot_test.cpp`:

- Add an env var such as `MNEMOS_MSX_VDP_IO_WATCH`.
- Wrap the installed CPU port input/output handlers after system assembly.
- Track the last traced PC in the CPU trace callback.
- For ports `$98`, `$99`, `$9A`, and `$9B`, log cycle, last PC, port, value,
  selected VDP status register (`R#15` on MSX2), and relevant VDP status bytes.
- Run `bean.rom` on MSX and MSX2 with PC watch around `$82F8-$8330` and compare
  the `IN A,($99)` result at the code that stores `$E3C5`.

Concrete commands to rerun the current diagnostics:

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
$env:MNEMOS_MSX2_BOOT_FRAMES='600'
$env:MNEMOS_MSX2_BOOT_SHA256='force-diagnostics'
$env:MNEMOS_MSX_PC_WATCH='$9900-$9A10'
& '.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe' '[golden][msx2]' 2>&1 |
  Tee-Object 'build\scratch\msx-bean-diagnostics\bean-msx2-pcwatch-9900-9a10.log'
```

## High-Value Source References

```text
src/chips/video/v9938/v9938.cpp
  status constants near top
  compose_status2()
  status_read()
  execute_command()
  update_irq()

src/manifests/msx/msx_system.cpp
  plain_32k_handoff_cart_slot()
  plain_16k_lower_page_visible()
  read_cart()
  read_memory()

src/manifests/msx2/msx2_system.cpp
  read_cartridge()
  plain_32k_handoff_cart_slot()
  plain_16k_lower_page_visible()
  read_slot()
  io_read() for VDP status at port $99
  io_write() for VDP control, palette, and indirect-register ports

src/manifests/common/msx_cartridge_mapper.cpp
  msx_plain_rom_physical_offset()
  mapper auto-detection and plain-ROM profile helpers

src/manifests/msx2/tests/msx2_system_test.cpp
  MSX2 plain 16 KiB cartridge visibility tests
  VDP frame IRQ wakes HALTed Z80 regression

tests/golden/msx_boot_test.cpp
  MSX/MSX2 firmware and ROM golden diagnostics
  PC watch infrastructure
```

## Definition Of Done For This Slice

At minimum, the next implementation slice should:

1. Explain why `bean.rom` selects `$99DB` on MSX2 and avoids it on MSX1.
2. Fix the root cause without broad cartridge mirroring hacks.
3. Add a focused regression for the fixed behavior.
4. Rebuild `mnemos_manifests_msx2_test` and `mnemos_msx_boot_test`.
5. Pass the skip-192 smoke window with `-RequireData`.
6. Run a wider contiguous MSX/MSX2 smoke window before claiming broader
   compatibility progress.

Do not mark the overall MSX/MSX2 goal complete until substantially broader
real-ROM validation exists.
