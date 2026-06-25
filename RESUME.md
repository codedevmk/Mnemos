# Irem Arcade Feature Handoff

Date: 2026-06-25

## Resume Point

- Worktree: `C:\dev\emu\Mnemos-irem-arcade`
- Branch: `feature/irem-arcade`
- Remote: `origin` -> `https://github.com/codedevmk/Mnemos.git`
- Resume from: `origin/feature/irem-arcade` after the 2026-06-25 M107 handoff commit and push.
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

- M15 has checked-in ROM-contract manifests and corpus gates.
- It remains non-executable. The next M15 slice should implement the Head On style i8080/discrete video/input/sound path and real screenshot smoke.

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
- `gallopm72` is incomplete locally: missing `mcu/cc_c-pr-.ic1`, size `0x1000`, CRC `0xac4421b1`.
- World `nspirit` is incomplete locally: missing `mcu/nin_c-pr-b.ic1`, size `0x1000`, CRC `0x0f7b2713`.
- `nspiritj` is a valid Japan variant and has `nin_c-pr-.ic1`, CRC `0x802d440a`; do not use it as proof for World `nspirit`.
- `MNEMOS_M72_SET_DIR` was intentionally unset in the latest full CTest run, so the full M72 roster golden test skipped. Dedicated M72 corpus smoke still passed for configured R-Type/protected/vertical artifacts.

## Latest Validation Evidence

Validation was run from `C:\dev\emu\Mnemos-irem-arcade` under:

```bat
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && <command>'
```

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

1. Continue M107 authenticity work: exact board clocks, V33/V30 facts, M107 memory/I/O map, sound CPU protocol, GA20/GA21/GA22 behavior, DIP behavior, raster timing, and screenshot parity.
2. Implement the M15 executable profile for `headoni`, including i8080/discrete video/input/sound path and real screenshot smoke.
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
