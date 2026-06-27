# MSX / MSX2 Resume Handoff

Generated: 2026-06-26T21:02:44-05:00
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote tracking branch: `origin/feature/msx2`
Pre-handoff HEAD: `8a820e0e Refresh MSX2 handoff state`

## User Goal

Implement both MSX and MSX2 in this worktree. They share common infrastructure, so fixes should land in shared MSX/MSX2 components wherever the behavior is common.

The user's local C-BIOS root is:

```text
D:\emu\msx\bios
```

Important user correction: a blank Mnemos Player window is not emulator proof. Player proof must explicitly pass both `--system msx|msx2` and `--rom <path>`.

## Current Verdict

MSX and MSX2 are not yet in a proven "100% working" state.

Current useful proof:

- Explicit player launches with real `--system` and `--rom` now run several MSX/MSX2 cartridge cases.
- MSX1 `Bestial Warrior` renders title art.
- MSX2 `Bestial Warrior` renders title art, but the running software/C-BIOS path rewrites the V9938 palette, so it is still not MSX1-faithful.
- MSX2 `AshGuine Story II` renders Japanese text/game scene.
- MSX1 `Boing-b` renders the game prompt screen.
- MSX1 and MSX2 `Bosconia` still remain on the C-BIOS logo.
- A V9938 reset-palette bug was fixed: the reset table now uses the same RGB333 packing as `palette_write`, and the regression test renders default-palette green/blue without manually seeding palette RAM.

Do not mark the goal complete until both MSX and MSX2 have ROM-backed player proof across representative cartridge paths, and the known Bosconia/color issues are resolved or explicitly triaged with accepted scope.

## Canonical Build And Test Commands

Per `README.md`, use the Windows MSVC preset from a Visual Studio developer environment:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug --output-on-failure'
```

Focused post-patch test slice:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "msx|MSX|tms9918a|v9938|mnemos_apps_player_system_launch_test|mnemos_apps_player_capability_summary_test" --output-on-failure'
```

Latest validation after the V9938 reset-palette patch:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_manifests_msx_test mnemos_manifests_msx2_test'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_manifests_msx_test|mnemos_manifests_msx2_test" --output-on-failure'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_player'
```

Focused tests passed: `mnemos_chips_video_v9938_test`, `mnemos_manifests_msx_test`, `mnemos_manifests_msx2_test`.

## Local Firmware And ROM Inputs

Verified C-BIOS files:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
```

Validated player ROM inputs used during the latest run:

```text
D:\emu\msx\MSX files [ROM]\Bestial Warrior.rom
D:\emu\msx\MSX files [ROM]\Avenger.rom
D:\emu\msx\MSX files [ROM]\AshGuine Story II.rom
D:\emu\msx\MSX files [ROM]\Boing-b.rom
D:\emu\msx\MSX files [ROM]\Bosconia.rom
```

The useful output directory from the latest explicit profile proof is:

```text
build\scratch\msx-profile-proof\20260626-202745
```

Additional current diagnostic output directories:

```text
build\scratch\msx-bosconia-firmware-sweep\20260626-204626
build\scratch\msx-bosconia-firmware-sweep\20260626-204726
build\scratch\msx-bosconia-mapper-sweep\20260626-205450
build\scratch\msx-v9938-palette-proof\20260626-205945
```

## Explicit Player Matrix

Use these environment variables before replaying the matrix:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_REGION='ntsc'
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_MSX2_REGION='ntsc'
```

Observed cases:

```text
bestial-msx    msx   ascii8   frames=3600  renders Bestial Warrior title art
bestial-msx2   msx2  ascii8   frames=3600  renders title art, color fidelity suspect
avenger-msx    msx   ascii8   frames=3600  screenshot produced, not visually rechecked in this handoff
ashguinz-msx2  msx2  generic8 frames=3600  renders Japanese text/game scene
boingb-msx     msx   plain    frames=3600  renders "PULSA <FUEGO> PARA JUGAR"
bosconia-msx   msx   plain    frames=3600  still C-BIOS logo
bosconia-msx2  msx2  plain    frames=3600  still C-BIOS logo
```

Important sidecar state:

```text
bosconia-msx.png.z80.regs.txt:
  AF=$0048 BC=$6D84 DE=$19C4 HL=$880C IX=$8010 SP=$F2FE PC=$108B IM=0 halted=true

bestial-msx.png.z80.regs.txt:
  AF=$0093 BC=$0001 DE=$1B00 HL=$94A9 IX=$405C SP=$F37C PC=$1804 IM=1

boingb-msx.png.z80.regs.txt:
  PC=$4E49
```

VDP/VRAM sidecars:

```text
bosconia-msx.png.tms9918a.regs.txt:
  R0=$02 R1=$E0 R2=$06 R3=$9F R4=$00 R5=$36 R6=$07 R7=$F5 frame=$E10
  VRAM remains logo-like, nonzero=5172

bestial-msx.png.tms9918a.regs.txt:
  R0=$02 R1=$E2 R2=$06 R3=$FF R4=$03 R5=$36 R6=$07 R7=$F1
  VRAM nonzero=11094

boingb-msx:
  VRAM nonzero=10629
```

Do not treat final `$E780 == 0` as the unique Bosconia root cause. The final value is also zero in working Bestial/Boing/Avenger runs.

## Bosconia Current Root-Cause Thread

The most useful current trace is:

```text
build\scratch\msx-bosconia-flow\bosconia-msx-240.png.cpu_trace.csv
```

It was generated with:

```powershell
$env:MNEMOS_CPU_TRACE='1'
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system msx --rom 'D:\emu\msx\MSX files [ROM]\Bosconia.rom' --mapper plain --screenshot build\scratch\msx-bosconia-flow\bosconia-msx-240.png --frames 240
Remove-Item Env:MNEMOS_CPU_TRACE
```

Trace row count: 247710.

Key PC counts from the 240-frame trace:

```text
0024=1
0047=14
0056=7
005C=31
0093=0
0138=1
108B=203
400F=1
4018=8192
4049=1
4052=1
4082=1
4085=0
409A=0
40A5=0
40AE=0
40BB=0
40C8=0
40D1=0
40E0=0
4126=3
491E=1
8708=1
8910=1
89CD=0
8AFB=1
8B08=1
FD9A=220
FD9F=220
F392=2
80BB=2
```

Interpretation:

- BIOS reaches Bosconia init at `$400F`.
- Bosconia copies CPU `$6000-$7FFF` to `$8000-$9FFF` with `LDIR`.
- Bosconia calls BIOS `$0138`, `$0024`, `$0047`, then copied code at `$8708`, `$8910`, `$491E`.
- Bosconia clears `$E000-$EFFF` and calls `$4126` three times.
- Bosconia reaches `$4082` and calls copied ROM/RAM code at `$8AFB`.
- Execution does not return to `$4085`.
- Execution therefore does not reach `$409A`, which is where Bosconia should write a `JP $40AE` hook into `$FD9A-$FD9C`.
- Later `$FD9A` hits are still C-BIOS default hook calls, not the Bosconia-installed hook.

C-BIOS bytes around final MSX1 PC `$108B`:

```text
... FB C9 76 10 FD C9 3A AF FC ...
```

So `$108B` is a BIOS HALT loop (`HALT; DJNZ -3; RET`).

Bosconia startup disassembly summary:

```text
400F LD HL,$6000
4012 LD DE,$8000
4015 LD BC,$2000
4018 LDIR
401A LD SP,$E800
401D CALL $0138
4036 CALL $0024
4039..4048 loop calling $0047
4049 CALL $8708
404C CALL $8910
404F CALL $491E
4052 clear $E000-$EFFF
405F..4074 setup via $4126
4077 LD HL,$3F00; LD BC,$0080; LD A,$BF; CALL $0056
4082 CALL $8AFB
4085 expected continuation if $8AFB returns
409A expected hook install: write $C3,$AE,$40 to $FD9A-$FD9C
40AE expected main loop hook target
40BB expected main loop over $E780
```

Current corrected Bosconia evidence:

- Bosconia still stays on the C-BIOS logo with MSX1 C-BIOS generic/BR/EU/JP and MSX2 C-BIOS generic/BR/EU/JP.
- Bosconia still stays on the C-BIOS logo with mapper overrides `auto`, `plain`, `ascii8`, `ascii16`, `generic8`, `konami`, and `konami-scc`.
- The current player trace at `build\scratch\msx-bosconia-slot-watch\bosconia-trace-240.png.cpu_trace.csv` shows `$4082 -> $8AFB`, then `$8AFC/$8AFD/...`, `$8B08`, `$88DB`, repeated `$4A1E` calls, `$8B21`, then `$0000`.
- This means Mnemos is executing the mirrored ROM bytes from Bosconia file offset `$0AFB`, not the copied RAM routine from file offset `$2AFB`.
- File offset `$0AFB` starts `49 E1 23 7E 32 23 E1 23 11 42 EE 06 06 CD DB 88...` and consumes the call return as inline data.
- Work RAM at `$8AFB` after the failed run contains the copied file-offset `$2AFB` routine (`21 00 E2 11 ...`), but that routine is not the one reached in the trace.
- Do not treat the older statement "`$8AFB` is copied RAM and should return to `$4085`" as proven.

Current focused question: should this 16 KiB lower-page Bosconia dump be mirrored into page 2 when the cartridge slot is selected there, or is the local dump/profile unsupported? Avoid changing the shared 16 KiB mirror rule without a hermetic regression, because existing tests intentionally cover both upper-page-entry mirroring and lower-page-only non-mirroring.

## Likely Next Steps

1. For Bosconia, verify the local dump against a known-good emulator or known-good ROM metadata before changing shared mapping semantics. The local SHA-256 is `1a76d08f33bf927b0e2977c13e62d6f87ece4a2275965c9134a436b852952368`.
2. If the dump is valid, isolate whether the 16 KiB lower-page mirror rule should be conditional on cartridge hardware/profile rather than global `plain`.
3. Add a hermetic MSX/MSX2 slot/mirror regression before changing shared cartridge behavior.
4. Continue MSX2 color triage separately from reset palette: the Bestial MSX2 run ends with palette `[$0000,$009F,$004F,$0000,$0049,$00DB,$016D,$01FF,$01F4,$01AB,$01EB,$01EA,$01F2,$01FA,$01E0,$01D0]`, so software or C-BIOS is actively rewriting palette RAM.
5. After patching, rerun the focused test slice and explicit player matrix.

## Relevant Code Surfaces

```text
src/apps/player/system_launch.cpp
src/apps/player/adapters/msx/msx_adapter.cpp
src/apps/player/adapters/msx2/msx2_adapter.cpp
src/apps/player/adapters/common/cli_args.cpp
src/apps/player/headless_commands.cpp
tests/golden/msx_boot_test.cpp
tests/golden/msx_rom_profiles.json
scripts/msx/run-boot-smoke.ps1
```

Notes:

- `--mapper` and `--mapper2` are supported by the player CLI.
- `system_launch.cpp` applies MSX/MSX2 machine-profile environment settings and passes mapper overrides.
- `scripts/msx/run-boot-smoke.ps1` has profile manifest support and applies mapper, slot, BIOS, and frame settings.
- Old raw-directory smoke runs can time out on `_BAD_` ROMs and are not a useful proof path.
- CPU trace is enabled with `MNEMOS_CPU_TRACE=1`; output goes beside the requested screenshot as `<screenshot>.cpu_trace.csv`.

## Do Not Do

- Do not use a blank player window as proof.
- Do not launch without explicit `--system` and `--rom`.
- Do not claim global MSX/MSX2 completion from Bestial/Boing/AshGuine alone.
- Do not chase final `$E780` zeros as the immediate root unless execution reaches `$40BB`.
- Do not treat build/test success alone as "100% working" without ROM-backed player evidence.

## Suggested Resume Command

```powershell
Set-Location C:\dev\emu\Mnemos-msx2
git status --short --branch
git log -1 --oneline
Get-Content .\RESUME.md
```
