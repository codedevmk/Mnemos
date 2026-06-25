# Irem Arcade Feature Handoff

Date: 2026-06-25

## Workspace

- Worktree: `C:\dev\emu\Mnemos-irem-arcade`
- Branch: `feature/irem-arcade`
- Remote: `origin` -> `https://github.com/codedevmk/Mnemos.git`
- Root checkout `C:\dev\emu\Mnemos` was intentionally not used for edits.
- The goal is still not complete: "100% working Irem arcade emulation" remains broader than the current proven slice.

## Current Commit Intent

This handoff is meant to be committed with every tracked and untracked change currently in the feature worktree. Resume from `feature/irem-arcade` at current `HEAD` after the commit/push that includes this file.

## What Is Implemented

- Irem M72 has a first-pass playable profile with checked-in true-M72 manifests, protected-set handling, MCS-51/i8751 plumbing, no-dump HLE declarations for selected no-dump sets, parent/clone loader hardening, media validation reporting, vertical/R-Type/protected gates, and corpus smoke tooling.
- Irem M82 has its own player-routable R-Type II profile via `--system irem_m82`, with wrapper ZIP/unpacked-folder handling, clone parent fallback, save/load, capability reporting, and real R-Type II data-gated smoke.
- Irem M81 now has:
  - checked-in manifests for `dbreed`, `hharry`, and `xmultipl`;
  - explicit V30/Z80/YM2151/DAC/8259 first-pass board assembly in `src/manifests/irem_m81`;
  - save-state identity across M81 board-layout profiles;
  - player adapter `src/apps/player/adapters/irem_m81`;
  - CLI/system-family routing via `--system irem_m81` and alias `m81`;
  - capability discovery and rollback-ready save-state reporting;
  - real local data-gated player smoke through `MNEMOS_M81_SET_DIR=D:\emu\irem\M81`.
- Irem M15, M84, and M107 have checked-in ROM-contract manifests and corpus gates, but remain non-executable board/profile surfaces.
- `scripts/run-data-gated-tests.ps1` now includes M81 player smoke in the Irem data-gated test selection.

## Local ROM Corpus State

Local Irem corpus roots used in validation:

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

Important corpus blockers:

- `gallopm72` is incomplete locally: missing `mcu/cc_c-pr-.ic1`, size `0x1000`, CRC `0xac4421b1`.
- World `nspirit` is incomplete locally: missing `mcu/nin_c-pr-b.ic1`, size `0x1000`, CRC `0x0f7b2713`.
- `nspiritj` is a separate valid Japan variant and has `nin_c-pr-.ic1`, CRC `0x802d440a`; do not use it as proof for World `nspirit`.
- `MNEMOS_M72_SET_DIR` was intentionally unset in the last full CTest run, so the full M72 roster golden test skipped. The dedicated M72 corpus smoke still passed for configured R-Type/protected/vertical artifacts.

## Latest Validation

All commands were run from `C:\dev\emu\Mnemos-irem-arcade` under:

```bat
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && <command>'
```

Results:

- `cmake --preset windows-msvc-debug` passed.
- Focused build passed:
  - `mnemos_apps_player_irem_m81_adapter_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_player`
- Focused CTest passed: `4/4`.
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_irem_m81_adapter_test`
  - `mnemos_apps_player_irem_m81_corpus_golden_test`
- Full build passed: `cmake --build --preset windows-msvc-debug`.
- Full CTest passed: `182/182`, with expected conformance/media skips.
- `scripts\run-data-gated-tests.ps1 -BuildDir build\windows-msvc-debug` passed:
  - selected tests: `21/21`;
  - M72 corpus smoke: `2/2`;
  - CPS2 smoke skipped because no CPS2 env was configured.

## Remaining Work

Do not mark the goal complete yet.

Highest-value continuation targets:

1. M84 executable board/profile: use the existing M84 manifests and local `hharry` parent from `D:\emu\irem\M81`; keep it separate from M81 until board wiring is proven.
2. M107 executable board/profile: Air Assault / Fire Barrel remain manifest-only; verify CPU/audio/video assumptions before implementing.
3. M15 executable profile for `headoni`: i8080/discrete video/input/sound path and real screenshot smoke are still open.
4. M72 artifact closure: continue searching for the exact Gallop and World Ninja Spirit MCU dumps, but do not substitute Japan `nspiritj` or fake fill bytes.
5. Authenticity pass: M81/M82 video priority, raster phase/timing, DIP behavior, palette banking, and board timing are first-pass, not final authentic parity.

## Suggested Resume Commands

```powershell
Set-Location C:\dev\emu\Mnemos-irem-arcade
git status --short --branch
git log -1 --oneline
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

For Irem data gates, set the environment variables listed above and run:

```powershell
scripts\run-data-gated-tests.ps1 -BuildDir build\windows-msvc-debug
```
