# Irem Arcade Feature Handoff

Date: 2026-06-25

## Resume Point

- Worktree: `C:\dev\emu\Mnemos-irem-arcade`
- Branch: `feature/irem-arcade`
- Remote: `origin` -> `https://github.com/codedevmk/Mnemos.git`
- Resume from: `origin/feature/irem-arcade` after the 2026-06-25 M15 handoff commit and push.
- Root checkout `C:\dev\emu\Mnemos` was intentionally not used for feature edits.
- This root-level `RESUME.md` is intentional because the user explicitly requested a handoff file at the workspace root.
- Do not mark the user goal complete. "100% working Irem arcade emulation" remains broader than the proven slice.

Quick verification after reopening:

```powershell
Set-Location C:\dev\emu\Mnemos-irem-arcade
git fetch origin
git status --short --branch
git log -1 --oneline
```

Expected state after this handoff: clean working tree on `feature/irem-arcade`, tracking `origin/feature/irem-arcade`.

## Current Implemented Coverage

### Irem M72

- First-pass playable profile with checked-in true-M72 manifests.
- Protected-set handling, MCS-51/i8751 plumbing, selected no-dump HLE declarations, parent/clone loader hardening, media validation reporting, vertical/R-Type/protected gates, and corpus smoke tooling.
- Real local R-Type/protected/vertical smoke passed with the configured local corpus.
- Remaining: artifact-proof closure for the full roster, plus authenticity work for video priority, raster timing, DIP behavior, palette behavior, and board-specific timing.

### Irem M81

- Checked-in manifests for `dbreed`, `hharry`, and `xmultipl`.
- Explicit V30/Z80/YM2151/DAC/8259 first-pass board assembly in `src/manifests/irem_m81`.
- Player adapter in `src/apps/player/adapters/irem_m81`.
- CLI/system-family routing via `--system irem_m81` and alias `m81`.
- Save-state identity across M81 board-layout profiles, capability discovery, rollback-ready save-state reporting, and real local data-gated smoke through `MNEMOS_M81_SET_DIR=D:\emu\irem\M81`.

### Irem M82

- Player-routable R-Type II profile via `--system irem_m82`.
- Wrapper ZIP/unpacked-folder handling, clone parent fallback, save/load, capability reporting, and real R-Type II data-gated smoke.

### Irem M84

- Checked-in manifests for `hharryb` and `hharryu`.
- M84-owned executable wrapper in `src/manifests/irem_m84`.
- The current executable M84 slice uses the M81-compatible V30/Z80/YM2151/DAC board core for local Hammerin' Harry split sets while preserving separate M84 manifest and save-state identity.
- Player adapter added at `src/apps/player/adapters/irem_m84`.
- CLI/system-family routing via `--system irem_m84` and alias `m84`.
- Clone-parent media routing composes M84 child media with supplemental M81 `hharry` parent media.
- Capability discovery, rollback-ready save-state reporting, and real local player smoke are data-gated through `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81`.
- Remaining: replace or verify the compatibility-core assumptions with board evidence for M84 memory/I/O behavior, Hammerin' Harry video/priority, raster timing, DIP behavior, and screenshot parity before calling this authentic.

### Irem M107

- M107 now has a first-pass executable board, not only ROM-contract metadata.
- Board implementation lives in `src/manifests/irem_m107/m107_system.hpp` and `src/manifests/irem_m107/m107_system.cpp`.
- The board owns a main V30, sound V30, M107 video diagnostic path, YM2151, OKI6295, 20-bit little-endian main/sound buses, RAM windows, I/O ports, frame stepping, and whole-board save/load with identity checks.
- Checked-in manifests and tests cover the local M107 sets currently embedded in the corpus gate, including `airass` and `firebarr`.
- Player adapter lives in `src/apps/player/adapters/irem_m107`.
- CLI/system-family routing is available through `--system irem_m107` and alias `m107`.
- Adapter accepts direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml`, and raw synthetic maincpu fallback.
- Capability discovery reports M107 memory views, V30 trace surfaces, YM2151/OKI6295 chip registers, rollback-ready save-state, and `media.rom_set state=available` for valid corpus media.
- Real local Air Assault player smoke wrote nonblank screenshots and successfully saved/loaded state.
- Remaining: this is still first-pass diagnostic rendering and executable wiring. Authentic M107 closure still needs V33/config proof if applicable, exact M107 memory/I/O map, GA20/GA21/GA22 video/priority behavior, PCM behavior, sound-CPU details, DIP behavior, raster timing, and screenshot parity.

### Irem M15

- M15 now has a first-pass executable board and player adapter for `headoni`, not only ROM-contract metadata.
- Board implementation lives in `src/manifests/irem_m15/m15_system.hpp` and `src/manifests/irem_m15/m15_system.cpp`.
- The board owns a MOS 6502 execution path via the shared `m6510` core in bare-6502 mode, M15 tile/color/chargen video path, 1-bit beeper, scratch/video/color/chargen RAM windows, input/DIP/control MMIO, frame IRQ pulse, and whole-board save/load with identity checks.
- The M15 map is now aligned with MAME/source metadata for Head On: scratch RAM `$0000-$02ff`, program ROM `$1000-$33ff`, vector ROM `$fc00-$ffff`, video RAM `$4000-$43ff`, color RAM `$4800-$4bff`, chargen RAM `$5000-$57ff`, P2 read `$a000`, sound write `$a100`, DIP read `$a200`, P1 read `$a300`, and control write `$a400`.
- The checked-in `headoni` manifest now uses full 64 KiB CPU address space and reloads `e4.9d` at `$fc00` for the 6502 reset/IRQ vectors.
- M15 inputs now follow Head On's active-high P1/P2 ports, `0x11` DIP default, and coin-triggered NMI edge; the active-low control flip bit is part of board state/save state.
- M15 video now renders from video/color/chargen RAM using the M-15 tile scan order and 1bpp palette lookup. The old program-ROM diagnostic fallback was removed.
- Player adapter lives in `src/apps/player/adapters/irem_m15`.
- CLI/system-family routing is available through `--system irem_m15` and alias `m15`.
- Adapter accepts direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml`, and raw synthetic maincpu fallback.
- Capability discovery reports 6502 trace/register surfaces, scratch/video/color/chargen RAM views, rollback-ready save-state, and `media.rom_set state=available` for valid corpus media.
- Real local Head On player smoke wrote nonblank screenshots and successfully saved/loaded state.
- Remaining: authentic M15 closure still needs discrete sample/sound behavior, analog color proof, raster timing, and screenshot parity.

## Local ROM Corpus Notes

Local Irem corpus roots used in the latest validation:

```powershell
$env:MNEMOS_M72_RTYPE_SET="D:\emu\irem\R-Type_Arcade_EN.zip"
$env:MNEMOS_M72_PROTECTED_SET="D:\emu\irem\imgfight.zip"
$env:MNEMOS_M72_VERTICAL_SET="D:\emu\irem\imgfight.zip"
$env:MNEMOS_M15_SET_DIR="D:\emu\irem\M15"
$env:MNEMOS_M81_SET_DIR="D:\emu\irem\M81"
$env:MNEMOS_M82_SET_DIR="D:\emu\irem"
$env:MNEMOS_M84_SET_DIR="D:\emu\irem\M84;D:\emu\irem\M81"
$env:MNEMOS_M107_SET_DIR="D:\emu\irem\M107"
```

Important local corpus facts:

- `D:\emu\irem\imgfightjb.zip` exists. ZIP versus unpacked folder is not the blocker; the loader supports both. The relevant distinction is split, merged, parent, and standalone composition.
- `D:\emu\irem\imgfight.zip`, `D:\emu\irem\imgfightj.zip`, `D:\emu\irem\imgfightj.7z`, `D:\emu\irem\imgfightjb.zip`, and `D:\emu\irem\imgfightjb.7z` are local corpus inputs for sorting Image Fight parent/clone composition.
- `D:\emu\irem\m72` has moved/expanded M72 artifacts, including `gallop.zip`, an unpacked `gallop\`, and an unpacked `nspirit\`.
- `scripts\irem_m72\find-missing-artifacts.ps1 -Root D:\emu\irem -Recurse -Set gallopm72,nspirit` now accepts the comma-separated set list and finds `42/44` artifacts present for those two sets. Missing entries include suggested local placement paths.
- `gallopm72` is incomplete locally: missing `mcu/cc_c-pr-.ic1`, size `0x1000`, CRC `0xac4421b1`.
- World `nspirit` is incomplete locally: missing `mcu/nin_c-pr-b.ic1`, size `0x1000`, CRC `0x0f7b2713`.
- If lawful dumps become available, the scanner points at these unpacked destinations: `D:\emu\irem\m72\gallop\cc_c-pr-.ic1` and `D:\emu\irem\m72\nspirit\nin_c-pr-b.ic1`. Equivalent ZIP entries are also valid if the matching set ZIP is rebuilt with the same filenames and CRCs.
- `nspiritj` is a valid Japan variant and has `nin_c-pr-.ic1`, CRC `0x802d440a`; do not use it as proof for World `nspirit`.
- Follow-up recursive zip/nested-zip scan across `D:\emu\irem` found only `D:\emu\irem\Ninja-Spirit_Arcade_JA.zip!nspiritj.zip!nin_c-pr-.ic1`, size `0x1000`, CRC `0x802d440a`; it did not find `gallopm72:mcu:cc_c-pr-.ic1:0xac4421b1` or `nspirit:mcu:nin_c-pr-b.ic1:0x0f7b2713`.
- `scripts\irem_m72\run-corpus-smoke.ps1 -RomDir D:\emu\irem\m72 -Recurse` currently passes `9/12` discovered M72 smoke groups. `gallopm72` and `nspirit` fail because `media_clean=False` from the missing MCU dumps. `imgfight` from `D:\emu\irem\m72\imgfight` alone fails because that raw folder lacks `if_c-pr-a.ic1`, but the mixed-source smoke `-Rom D:\emu\irem\imgfight.zip,D:\emu\irem\m72\imgfight` passes `1/1`.
- `MNEMOS_M72_SET_DIR` was intentionally unset in the latest full CTest run, so the full M72 roster golden test skipped. Do not set it to the current partial `D:\emu\irem\m72` tree and call that a full-roster proof; use the smoke runner for partial corpus evidence until all authentic roster artifacts are present.

## Latest Validation Evidence

Validation was run from `C:\dev\emu\Mnemos-irem-arcade` under:

```bat
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && <command>'
```

Final handoff validation completed on 2026-06-25 21:05 -05:00:

- Full preset build: `cmake --build --preset windows-msvc-debug`
- Full preset CTest: `188/188`, with expected conformance/media skips where ROM or oracle env vars were unset
- Data-gated script with local Irem env vars: `24/24` selected tests, `0` failures
- Irem M72 corpus smoke from the data-gated script: `2/2` passed for configured R-Type/protected/vertical artifacts
- M72 roster golden still skipped because `MNEMOS_M72_SET_DIR` was intentionally unset; Gallop and World Ninja Spirit remain blocked by missing local MCU dumps
- CPS2 corpus smoke skipped because no CPS2 env vars were set for this Irem handoff

Continuation validation on 2026-06-25 21:19 -05:00:

- `scripts\irem_m72\find-missing-artifacts.ps1 -Root D:\emu\irem -Recurse -Set gallopm72,nspirit`: `42/44` present, missing only `cc_c-pr-.ic1` and `nin_c-pr-b.ic1`, with suggested placements under `D:\emu\irem\m72\gallop\` and `D:\emu\irem\m72\nspirit\`
- Same artifact command with `-Set 'gallopm72,nspirit'` validates the comma-separated set-list path
- `scripts\irem_m72\run-corpus-smoke.ps1 -Rom D:\emu\irem\imgfight.zip,D:\emu\irem\m72\imgfight -MaxSets 1`: `1/1` passed
- `scripts\irem_m72\run-corpus-smoke.ps1 -Rom D:\emu\irem\m72\gallop.zip,D:\emu\irem\m72\gallop -MaxSets 1`: expected failure with `media_clean=False`, lit screenshot, and missing `cc_c-pr-.ic1`

M107 slice validation that passed:

- `cmake --preset windows-msvc-debug`
- Focused M107/player build:
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_player`
- Focused CTest: `4/4`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
- M107 data-gated CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `2/2`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_corpus_golden_test`
- Direct M107 player capability smoke:
  - ROM: `D:\emu\irem\M107\airass.zip`
  - command included `--system irem_m107 --capabilities`
  - output reported `media.rom_set state=available`, rollback available, V30 trace surfaces, YM2151/OKI6295 registers, and M107 RAM views
- Screenshot smoke:
  - command included `--system irem_m107 --rom "D:\emu\irem\M107\airass.zip" --screenshot build\scratch\irem_m107_airass.ppm --frames 120`
  - wrote `384x256` PPM with nonzero pixel payload
- Save/load smoke:
  - save state: `build\scratch\irem_m107_airass.state`, 181160 bytes
  - loaded screenshot: `build\scratch\irem_m107_airass_loaded.ppm`, `384x256`, nonzero pixel payload

M15 legacy placeholder validation that passed after the M107 handoff, superseded by the 6502 correction below:

- `cmake --preset windows-msvc-debug`
- Focused M15/player build:
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_irem_m15_adapter_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_player`
- Focused CTest: `4/4`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_irem_m15_adapter_test`
- M15 data-gated CTest with `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`: `2/2`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_irem_m15_corpus_golden_test`
- Direct M15 player capability smoke:
  - ROM: `D:\emu\irem\M15\Head-On_Arcade_EN.zip`
  - command included `--system irem_m15 --capabilities`
  - output reported `media.rom_set state=available`, rollback available, legacy CPU trace/register surfaces, and M15 RAM views
- Screenshot smoke:
  - command included `--system irem_m15 --rom "D:\emu\irem\M15\Head-On_Arcade_EN.zip" --screenshot build\scratch\irem_m15_headoni.ppm --frames 120`
  - wrote `224x256` PPM with nonzero pixel payload
- Save/load smoke:
  - save state: `build\scratch\irem_m15_headoni.state`, 15230 bytes
  - loaded screenshot: `build\scratch\irem_m15_headoni_loaded.ppm`, `224x256`, nonzero pixel payload

M15 legacy opcode-coverage continuation validation, superseded by the 6502 correction below:

- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m15_test mnemos_apps_player_irem_m15_adapter_test mnemos_apps_player_capability_summary_test mnemos_player`
- Focused CTest: `3/3`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_irem_m15_adapter_test`
- M15 data-gated CTest with `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`: `2/2`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_irem_m15_corpus_golden_test`

M15 6502 correction validation on 2026-06-25 22:41 -05:00:

- Configure plus focused build:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_chips_cpu_m6510_test mnemos_manifests_irem_m15_test mnemos_apps_player_irem_m15_adapter_test mnemos_apps_player_capability_summary_test`
  - `cmake --build --preset windows-msvc-debug --target mnemos_player`
- Focused CTest with `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`: `6/6`, with `mnemos_chips_cpu_m6510_conformance_test` skipped because the external 6502 conformance corpus is absent
  - `mnemos_chips_cpu_m6510_test`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_irem_m15_adapter_test`
  - `mnemos_apps_player_irem_m15_corpus_golden_test`
- Direct M15 player capability smoke:
  - ROM: `D:\emu\irem\M15\Head-On_Arcade_EN.zip`
  - command included `--system irem_m15 --capabilities`
  - output reported `debug.6502.cpu_trace`, `memory.6502.registers`, `memory.system.scratch_ram`, `memory.system.video_ram`, `memory.system.color_ram`, `memory.system.chargen_ram`, rollback available, and `media.rom_set state=available`
- Screenshot smoke:
  - command included `--system irem_m15 --rom "D:\emu\irem\M15\Head-On_Arcade_EN.zip" --screenshot "C:\dev\emu\Mnemos-irem-arcade\build\scratch\irem_m15_headoni_6502.ppm" --frames 120`
  - wrote `224x256` PPM, 172047 bytes, nonzero RGB payload
- Save/load smoke:
  - save state: `build\scratch\irem_m15_headoni_6502.state`, 3061 bytes after 60 frames
  - loaded screenshot: `build\scratch\irem_m15_headoni_6502_loaded.ppm`, `224x256`, 172047 bytes, nonzero RGB payload

M15 input/video authenticity continuation validation on 2026-06-26:

- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m15_test mnemos_apps_player_irem_m15_adapter_test mnemos_apps_player_capability_summary_test mnemos_player`
- Focused CTest with `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`: `4/4`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_irem_m15_adapter_test`
  - `mnemos_apps_player_irem_m15_corpus_golden_test`
- Direct M15 player screenshot smoke:
  - command included `--system irem_m15 --rom "D:\emu\irem\M15\Head-On_Arcade_EN.zip" --screenshot "C:\dev\emu\Mnemos-irem-arcade\build\scratch\irem_m15_headoni_active_high_120.ppm" --frames 120`
  - wrote `224x256` PPM, 172047 bytes, nonzero RGB payload
  - same check at `600` frames also produced a nonzero RGB payload
- Direct M15 player capability smoke:
  - command included `--system irem_m15 --rom "D:\emu\irem\M15\Head-On_Arcade_EN.zip" --capabilities`
  - output reported 6502 trace/registers, M15 RAM views, input ports, rollback, and `media.rom_set state=available`
- Direct M15 player save/load smoke:
  - save state: `build\scratch\irem_m15_headoni_active_high.state`, 1597 bytes after 120 frames
  - loaded screenshot: `build\scratch\irem_m15_headoni_active_high_loaded.ppm`, `224x256`, 172047 bytes, nonzero RGB payload
- Full validation after this slice:
  - `cmake --build --preset windows-msvc-debug`: no work to do
  - `ctest --preset windows-msvc-debug --output-on-failure`: `188/188`, expected corpus/conformance skips

Earlier branch validation that passed before the M107 slice:

- M84 focused build and focused CTest: `4/4`
- M84 data-gated CTest: `2/2`
- M84 real player capability/screenshot/save-load smoke against local Hammerin' Harry child plus M81 parent media
- Full build: `cmake --build --preset windows-msvc-debug`
- Full CTest: `184/184`, with expected conformance/media skips
- Data-gated script selected tests: `22/22`
- M72 corpus smoke: `2/2`

Repository hygiene notes:

- `git diff --check` was clean except recurring LF-to-CRLF working-copy warnings in this Windows checkout.
- `python tools\governance\repo_hygiene.py --all` flags root `RESUME.md`; this is an intentional user-requested handoff exception, not a source-layout regression.

## Suggested Next Work

1. Continue M15 authenticity work: discrete sample/sound behavior, analog color proof, raster timing, and screenshot parity.
2. Continue M107 authenticity work: exact board clocks, V33/V30 facts, M107 memory/I/O map, sound CPU protocol, GA20/GA21/GA22 behavior, DIP behavior, raster timing, and screenshot parity.
3. Do the M84 authenticity pass and replace or validate the M81-compatible assumptions.
4. Continue M72 artifact closure by locating exact Gallop and World Ninja Spirit MCU dumps. Do not substitute Japan `nspiritj` or synthetic fill bytes.
5. Continue authenticity passes for M81/M82/M72 video priority, raster phase/timing, DIP behavior, palette banking, and board timing.

## Resume Commands

Baseline:

```powershell
Set-Location C:\dev\emu\Mnemos-irem-arcade
git status --short --branch
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Data gates:

```powershell
$env:MNEMOS_M72_RTYPE_SET="D:\emu\irem\R-Type_Arcade_EN.zip"
$env:MNEMOS_M72_PROTECTED_SET="D:\emu\irem\imgfight.zip"
$env:MNEMOS_M72_VERTICAL_SET="D:\emu\irem\imgfight.zip"
$env:MNEMOS_M15_SET_DIR="D:\emu\irem\M15"
$env:MNEMOS_M81_SET_DIR="D:\emu\irem\M81"
$env:MNEMOS_M82_SET_DIR="D:\emu\irem"
$env:MNEMOS_M84_SET_DIR="D:\emu\irem\M84;D:\emu\irem\M81"
$env:MNEMOS_M107_SET_DIR="D:\emu\irem\M107"
scripts\run-data-gated-tests.ps1 -BuildDir build\windows-msvc-debug
```
