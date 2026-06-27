# MSX / MSX2 Resume Handoff

Generated: 2026-06-27 00:39:05 -05:00 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote branch: `origin/feature/msx2`
Previous pushed tip before this handoff commit: `355c5162`

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
- The local ROM corpus used here is `D:\emu\msx\MSX files [ROM]`.
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

Do not commit any of those input files.

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- MSX and MSX2 C-BIOS boot paths run through golden tests and explicit player
  launches when invoked with system/media arguments.
- MSX and MSX2 share RAM-size profile semantics through the manifest, player
  launch, adapter, smoke script, and golden-test paths.
- `3D Pool [cas2rom64ks]` auto-detects as ASCII16 through the shared mapper
  resolver and boots through both MSX and MSX2.
- `a_test2 [Arabic MSX].rom` is profiled for MSX2 BR C-BIOS instead of being
  treated as a V9938 renderer failure.
- `abbaye_v1.1.rom` resolves its strong ASCII8 loader-write signature before a
  lower-page self-modifying-code hit can misclassify it as Generic8.
- The MSX/MSX2 smoke runner treats empty/zero-byte logs as "no hash" rather
  than throwing from the regex parser.
- Bounded real-ROM smoke windows have passed through skip 191. The most recent
  fully passing window before the active blocker was `-SkipRoms 180 -MaxRoms 12`
  with `25/25` passing.

Known gaps:

- The current active blocker is the skip-192 corpus window: `bean.rom` passes
  on MSX, but fails on MSX2 because the framebuffer remains uniform after retry.
- This is not yet a representative compatibility matrix.
- Earlier notes included Bosconia staying on the C-BIOS logo and MSX2 Bestial
  Warrior color fidelity suspicion; those still need confirmation in later
  slices.

## Current Checkpoint Contents

This checkpoint includes one source change plus this handoff:

- `tests/golden/msx_boot_test.cpp`: adds opt-in VDP I/O port diagnostics behind
  `MNEMOS_MSX_VDP_IO_WATCH`.
- `RESUME.md`: current handoff and resume point for the next agent.

The VDP I/O watch logs MSX/MSX2 VDP port access for ports `$99`, `$9A`, and
`$9B`. Port `$98` is intentionally excluded to avoid flooding logs. The watch
does not use the single Z80 trace hook; it wraps CPU I/O callbacks, so it can be
combined with `MNEMOS_MSX_PC_WATCH`.

Accepted `MNEMOS_MSX_VDP_IO_WATCH` values:

```text
1
true
all
$82F0-$8340
```

A range value logs VDP I/O only when the CPU PC is inside that range.

## Latest Build Evidence

After adding the VDP I/O watch, this focused build passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_msx_boot_test'
```

No full `ctest` sweep was run after these diagnostic-only test edits.

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

MSX `bean.rom` hash:

```text
690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a
```

MSX2 `bean.rom` hash:

```text
9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573
```

The MSX2 hash differs from the firmware baseline, but Catch2 failed because
the framebuffer was uniform after 3600 frames.

## Bean.rom Diagnostic State

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

## Latest VDP I/O Watch Evidence

Use this filtered MSX2 diagnostic command:

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
$env:MNEMOS_MSX_PC_WATCH='$82F0-$8340'
$env:MNEMOS_MSX_VDP_IO_WATCH='$82F0-$8340'
& '.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe' '[golden][msx2]' 2>&1 |
  Tee-Object 'build\scratch\msx-bean-diagnostics\bean-msx2-vdp-io-watch-82f0-8340.log'
```

This run fails intentionally because `MNEMOS_MSX2_BOOT_SHA256` is set to a
forced mismatch. Use the log, not the test exit, as the diagnostic artifact.

Critical VDP I/O evidence:

```text
in pc=$8305 cycles=14093184 port=$99 value=$88 selected_s=0 selected_value=$08 s0=$08 s1=$01 s2=$4E ... ix=$832E ... r0=$00 r1=$80 r2=$06 r7=$F4 r15=$00 e12d=$00 e3c5=$00
in pc=$8305 cycles=14562955 port=$99 value=$C4 selected_s=0 selected_value=$04 s0=$04 s1=$01 s2=$4E ... ix=$8D7C ... r0=$02 r1=$E2 r2=$06 r7=$F4 r15=$00 e12d=$00 e3c5=$88
in pc=$8305 cycles=14563297 port=$99 value=$04 selected_s=0 selected_value=$04 s0=$04 s1=$01 s2=$4E ... ix=$9463 ... e3c5=$C4
in pc=$8305 cycles=14564741 port=$99 value=$04 selected_s=0 selected_value=$04 s0=$04 s1=$01 s2=$4E ... ix=$99CD ... e3c5=$04
```

Interpretation:

- `bean.rom` on MSX2 repeatedly reads VDP status register 0 at ROM `$8303`;
  the logged PC is `$8305`, immediately after `IN A,($99)`.
- The final path-selection read returns `$04`: no high status flags, only low
  sprite overflow index bits.
- That status low-bit residue participates in the indirect table path that
  eventually reaches `$99DB`, then `CALL $BFFF`, then `$C000` RAM.
- The same range-filtered MSX1 diagnostic produced no VDP I/O events in the
  selector range and kept running at `$829D`, reinforcing that MSX1 does not
  take the problematic selector path in the 600-frame window.

## ROM Bytes Around The Selector

Useful bytes from `bean.rom`:

```text
82F0: 82 11 0A 00 CD F8 82 C9 DD 21 2E 83 3A 2D E1 B7
8300: C4 3D 82 DB 99 32 C5 E3 FB DD 7E 01 B7 C8 DD E5
8310: D5 DD 19 DD 6E 00 DD 66 01 CD 2D 83 D1 DD E1 CD
8320: 9D 82 DD 6E 00 DD 66 01 E5 DD E1 18 CF E9 7C 8D
8330: 3C 83 E8 84 E9 84 DC 85 74 87 DB 87 01 02 00 CD
99C0: 02 05 00 FF 2F 00 5F 06 80 07 80 00 00 28 A6 DB
99D0: 99 45 9C 46 9C 56 9C 57 9C 3C A1 21 FD 99 11 40
99E0: 00 06 06 CD FF BF 21 2D 9A 11 58 00 06 04 CD DF
99F0: BF 21 4D 9A 11 60 00 06 10 CD FF BF C9 00 00 01
```

Manual notes:

- `$82F8`: `LD IX,$832E`.
- `$82FC`: `LD A,($E12D)`.
- `$8300`: `CALL NZ,$823D`.
- `$8303`: `IN A,($99)`.
- `$8305`: `LD ($E3C5),A`.
- `$8309`: `LD A,(IX+1)`.
- `$830D`: `RET Z`.
- `$8311`: `ADD IX,DE`.
- `$8313/$8316`: load a pointer from the table.
- `$8319`: `CALL $832D`.
- `$832D`: `JP (HL)`.

The selected status byte and `DE` offsets drive an indirect table walk.

## Current Hypothesis

The most plausible current root-cause area is V9938 status-register-0 sprite
overflow/index behavior or timing:

- The final MSX2 selector read sees `S0=$04`, which appears to be the low five
  sprite overflow index bits after the upper status flags have been cleared.
- The existing V9938 implementation clears S0 upper bits on `status_read()` but
  preserves the low five bits, matching the TMS9918A behavior already asserted
  by tests.
- Do not blindly clear V9938 S0 lower bits. If V9938 differs from TMS9918A, add
  a focused V9938 regression backed by documentation or a strong oracle.
- A likely angle is whether V9938 S0 lower bits should reset to `$1F` or another
  no-overflow value at frame/scanline boundaries, or whether Mnemos is generating
  sprite overflow/index too early under the game mode/timing.
- Another angle is whether the exact V9938 mode/sprite-table state at the final
  selector read needs more logging. Current VDP I/O event output does not include
  `r5`, `r6`, `r8`, `r9`, frame, or decoded mode.

Relevant source surfaces:

- `src/chips/video/v9938/v9938.cpp`
- `src/chips/video/tms9918a/tms9918a.cpp`
- `src/chips/video/tms9918a/tests/tms9918a_test.cpp`
- `tests/golden/msx_boot_test.cpp`
- `scripts/msx/run-boot-smoke.ps1`
- `tests/golden/msx_rom_profiles.json`
- `src/manifests/msx2/`
- `src/apps/player/adapters/msx2/`

## Suggested Next Steps

1. Add `r5`, `r6`, `r8`, `r9`, frame, decoded mode, and perhaps status-before-read
   to the VDP I/O event line.
2. Rebuild `mnemos_msx_boot_test`.
3. Rerun the filtered MSX2 `bean.rom` diagnostic with
   `MNEMOS_MSX_VDP_IO_WATCH='$82F0-$8340'`.
4. Confirm the exact V9938 mode/sprite-table state at the final `$8303` status
   read.
5. Add a focused V9938 S0 sprite overflow/no-overflow regression before changing
   status semantics.
6. Rerun the skip-192 smoke window after a real fix.
7. Only then run broader `ctest --preset windows-msvc-debug --output-on-failure`.

## Validation Commands

Focused rebuild:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_msx_boot_test'
```

Focused test:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_msx_boot_test" --output-on-failure'
```

Full preset sweep:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug --output-on-failure'
```

Skip-192 smoke after the fix:

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
- The fix has a focused regression in chip/system/golden coverage as appropriate.
- Explicit player launches use `--system` and `--rom` and render non-blank,
  game-specific output.
- A broader MSX/MSX2 corpus window passes after the skip-192 fix.
- The Windows MSVC preset build and relevant tests pass.
