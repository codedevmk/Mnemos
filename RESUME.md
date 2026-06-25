# Irem Arcade Feature Handoff

Date: 2026-06-25

## Resume Point

- Worktree: `C:\dev\emu\Mnemos-irem-arcade`
- Branch: `feature/irem-arcade`
- Remote: `origin` -> `https://github.com/codedevmk/Mnemos.git`
- Resume from: `origin/feature/irem-arcade` after the 2026-06-25 handoff push.
- Root checkout `C:\dev\emu\Mnemos` was intentionally not used for feature edits.
- This root-level `RESUME.md` is intentional because the user explicitly requested the handoff at this path.
- Do not mark the user goal complete. "100% working Irem arcade emulation" remains broader than the proven slice.

Quick verification after reopening:

```powershell
Set-Location C:\dev\emu\Mnemos-irem-arcade
git fetch origin
git status --short --branch
git log -1 --oneline
```

Expected branch state after this handoff: clean working tree on `feature/irem-arcade`, tracking `origin/feature/irem-arcade`.

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

### Irem M15 and M107

- M15 and M107 have checked-in ROM-contract manifests and corpus gates.
- They remain non-executable board/profile surfaces.
- Highest-value next implementation target is M107, but inspect the board facts first. Do not silently relabel another board core as M107 unless the work is explicitly scoped as a first-pass compatibility wrapper with separate identity and documented limitations.

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

- `D:\emu\irem\imgfightjb.zip` exists. ZIP vs unpacked folder is not the blocker; the loader supports both. The important distinction is split, merged, parent, and standalone composition.
- `gallopm72` is incomplete locally: missing `mcu/cc_c-pr-.ic1`, size `0x1000`, CRC `0xac4421b1`.
- World `nspirit` is incomplete locally: missing `mcu/nin_c-pr-b.ic1`, size `0x1000`, CRC `0x0f7b2713`.
- `nspiritj` is a valid Japan variant and has `nin_c-pr-.ic1`, CRC `0x802d440a`; do not use it as proof for World `nspirit`.
- `MNEMOS_M72_SET_DIR` was intentionally unset in the latest full CTest run, so the full M72 roster golden test skipped. The dedicated M72 corpus smoke still passed for configured R-Type/protected/vertical artifacts.

## Latest Validation Evidence

All validation was run from `C:\dev\emu\Mnemos-irem-arcade` under:

```bat
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && <command>'
```

Passed results:

- `cmake --preset windows-msvc-debug`
- Focused M84/player build:
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_irem_m84_adapter_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_player`
- Focused CTest: `4/4`
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_irem_m84_adapter_test`
- M84 data-gated CTest: `2/2`
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_irem_m84_corpus_golden_test`
- Real player M84 smoke:
  - child: `D:\emu\irem\M84\Hammerin-Harry_Arcade_EN.zip`
  - parent: `D:\emu\irem\M81\Hammerin-Harry_Arcade_EN.zip`
  - capability summary reported `media.rom_set state=available` and rollback available
  - screenshot smoke wrote `build\scratch\irem_m84_hharryu.ppm`, `384x256`, nonzero pixel payload
  - save/load smoke wrote `build\scratch\irem_m84_hharryu.state` and `build\scratch\irem_m84_hharryu_loaded.ppm`, also nonzero
- Full build: `cmake --build --preset windows-msvc-debug`
- Full CTest: `184/184`, with expected conformance/media skips
- Data-gated script:
  - `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\run-data-gated-tests.ps1 -BuildDir build\windows-msvc-debug`
  - selected tests: `22/22`
  - M84 corpus golden passed
  - M72 corpus smoke: `2/2`
  - CPS2 smoke skipped because no CPS2 env was configured
- `git diff --check` was clean except recurring LF-to-CRLF working-copy warnings.
- `python tools\governance\repo_hygiene.py --all` flags root `RESUME.md`; this is an intentional user-requested handoff exception, not a source-layout regression.

## Suggested Next Work

1. Implement the M107 executable board/profile for Air Assault / Fire Barrel after inspecting `src/manifests/irem_m107`, local `D:\emu\irem\M107`, and comparable Irem board cores.
2. Implement the M15 executable profile for `headoni`, including i8080/discrete video/input/sound path and real screenshot smoke.
3. Do the M84 authenticity pass and replace/validate the M81-compatible assumptions.
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
