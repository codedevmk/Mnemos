# MSX / MSX2 Handoff

Generated: 2026-06-27 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote: `origin/feature/msx2`
Base before this handoff checkpoint: `97fab267`

This handoff exists because the original Codex session ran for roughly 30 hours
and hit practical context-window limits. Continue from this file and the live
worktree state instead of reconstructing the full chat.

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
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

## User Contract

- Implement both MSX and MSX2; the systems share common manifest, mapper,
  player adapter, VDP, script, and golden-test surfaces.
- Preserve the requested worktree and branch: `feature/msx2`.
- C-BIOS is under `D:\emu\msx\bios`.
- The ROM corpus used for this slice is `D:\emu\msx\MSX files [ROM]`.
- Do not claim "100% working" until real ROM/media validation proves it.
- A blank Mnemos Player window is not proof; launch with explicit `--system`
  and `--rom`, or use the data-gated smoke runner.
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
- The latest focused V9938 and MSX boot tests pass.

Active blocker:

- Skip-192 still fails on MSX2 for `bean.rom`.
- MSX `bean.rom` passes in the same smoke window.
- MSX2 `bean.rom` exits with code 42 because the framebuffer remains uniform
  after the 3600-frame retry.
- Do not mark the goal complete until this and broader real-ROM coverage pass.

## Current Checkpoint Contents

This checkpoint includes:

- `tests/golden/msx_boot_test.cpp`: enriches opt-in VDP I/O diagnostics with
  V9938 frame index, decoded display mode, and registers `r5`, `r6`, `r8`,
  `r9`, `r11`, and `r23`.
- `src/chips/video/v9938/v9938.cpp`: changes S#0 reads so they clear only the
  frame interrupt bit and preserve sprite overflow, collision, and low sprite
  index state.
- `src/chips/video/v9938/tests/v9938_test.cpp`: updates and adds focused V9938
  S#0 regressions for preserved sprite overflow and collision state.
- `RESUME.md`: this handoff.

Rationale for the V9938 change:

- Yamaha V9938 documentation describes S#0 bit F as reset by reading S#0.
- It does not describe S#0 sprite overflow/collision bits as reset by the read.
- The new test coverage locks that behavior down.
- This change is correct and test-backed, but it does not fix `bean.rom`.

Reference used during diagnosis:

```text
https://archive.org/stream/bitsavers_yamahaYamanicalDataBookAug85_6932685/Yamaha_V9938_MSX-Video_Technical_Data_Book_Aug85_djvu.txt
```

## Latest Validation Evidence

Focused build and test command:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

Result:

```text
2/2 tests passed:
mnemos_chips_video_v9938_test
mnemos_msx_boot_test
```

Latest skip-192 smoke command:

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

Artifacts:

```text
C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260627-004638-607-81136\summary.json
C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260627-004638-607-81136\019-msx2-rom-bean.log
C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260627-004638-607-81136\019-msx2-rom-bean-retry-3600.log
```

Do not commit these artifacts.

## Bean.rom Diagnostic State

ROM:

```text
D:\emu\msx\MSX files [ROM]\bean.rom
Size: 16384 bytes
Header starts at offset 0: 41 42 04 80 ...
Init vector: $8004
Mapper: Plain
```

Important MSX2 path:

- The bad path reaches `$99DB`, then calls `$BFFF`.
- `$BFFF` is the final byte of the 16 KiB ROM and contains `C5`.
- Execution continues into `$C000` RAM and eventually reaches a bad HALT at
  `$CA3E`.
- Final slot state remains `primary=$D0`, meaning pages 0/1 BIOS, page 2
  cartridge, page 3 RAM.
- Current MSX2 RAM mapper segments at failure are `[3,2,1,0]`.

Narrow PC watch evidence from the prior diagnostic:

```text
range previous_pc=$99E3 current_pc=$BFFF cycles=14564941 ... sp=$E3AF ret0=$99E6
prev_code=[$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11,$99DF=$40,$99E0=$00,$99E1=$06,$99E2=$06,$99E3=$CD,$99E4=$FF,$99E5=$BF,$99E6=$21,...]
code=[$BFF7=$01,$BFF8=$08,$BFF9=$00,$BFFA=$09,$BFFB=$C1,$BFFC=$10,$BFFD=$E1,$BFFE=$C9,$BFFF=$C5,$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00]
```

Latest enriched VDP I/O diagnostic after the S#0 preservation fix:

```text
Log:
build\scratch\msx-bean-diagnostics\bean-msx2-vdp-io-watch-82f0-8340-s0-preserve.log

First selector read:
pc=$8305 cycles=14093184 port=$99 value=$C4 selected_value=$44 s0=$44 frame=235 mode=graphics_i r0=$00 r1=$80 r2=$06 r5=$36 r6=$07 r8=$08 r9=$00 r11=$00 r23=$00

Later selector state:
value=$C4, then $44, then $44
frame=243 mode=graphics_ii r0=$02 r1=$E2 r2=$06 r5=$36 r6=$07 r8=$08 r9=$00 r11=$00 r23=$00
```

Interpretation:

- Before the S#0 fix, the final selector reads became `$04` because S#0 reads
  cleared the high sprite status bits.
- After the fix, the selector sees `$44`, but `bean.rom` still takes the bad
  path.
- The root cause likely moved away from V9938 S#0 read-clearing and toward the
  code/data path around `$BFFF`/`$C000`, RAM preparation, slot state, or RAM
  mapper segment visibility.

## Relevant Source Surfaces

- `src/chips/video/v9938/v9938.cpp`
- `src/chips/video/v9938/v9938.hpp`
- `src/chips/video/v9938/tests/v9938_test.cpp`
- `src/chips/video/tms9918a/tms9918a.cpp`
- `tests/golden/msx_boot_test.cpp`
- `scripts/msx/run-boot-smoke.ps1`
- `tests/golden/msx_rom_profiles.json`
- `src/manifests/common/msx_cartridge_mapper.cpp`
- `src/manifests/msx/msx_system.cpp`
- `src/manifests/msx2/msx2_system.cpp`
- `src/apps/player/adapters/msx2/`

Mapping details to preserve:

- `msx_plain_rom_physical_offset(...)` maps plain ROM reads below `$C000`.
- MSX/MSX2 plain <=32 KiB cartridges return `0xFF` outside `$4000-$BFFF`.
- Existing 32 KiB handoff logic is deliberately limited to 32 KiB plain ROMs.
- Do not blindly map page 3 as cartridge for this 16 KiB ROM without a strong
  regression and rationale.

## Suggested Next Slice

1. Add an opt-in diagnostic for writes to `$C000-$C020`, ideally test-only and
   combinable with the existing PC and VDP I/O watches.
2. Compare MSX and MSX2 `bean.rom` writes/reads/execution around
   `$BFF0-$C020`.
3. Determine whether the expected `$C000` trampoline/code is missing, copied to
   a different RAM mapper segment, or hidden by slot/mapper state.
4. If the issue is RAM mapper or slot visibility, add a focused system/golden
   regression before changing behavior.
5. Rerun the skip-192 smoke window.
6. Only after the blocker passes, run broader MSX/MSX2 corpus windows and the
   Windows MSVC preset tests.

## Completion Bar

Do not mark the MSX/MSX2 goal complete until:

- `bean.rom` passes both MSX and MSX2 in the skip-192 smoke window.
- The fix has focused regression coverage in chip/system/golden tests as
  appropriate.
- Explicit player launches use `--system` and `--rom` and render non-blank,
  game-specific output.
- A broader MSX/MSX2 corpus window passes after the skip-192 fix.
- The Windows MSVC preset build and relevant tests pass.
