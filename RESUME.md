# Irem Arcade Feature Handoff

Date: 2026-06-26

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
- `dbreedm72` and `dkgensanm72` no-dump MCU HLE profiles now cover startup inversion, entry continuation stubs, command-latch acknowledge, sample-trigger cursor setup, and profile-specific service-ROM checksum response bytes.
- Palette RAM now exposes the M72 CPU-visible disconnected-A9 mirror and low-byte-only 5-bit gun behavior while keeping canonical R/G/B plane storage for rendering and save states.
- The M72 player adapter now consumes explicit arcade `service` and `test` frontend inputs: service 1/2 map active-low to system bits 4/5, `mode` remains a legacy service alias, operator test maps active-low to bit 6, and adapter save-state version 2 persists those fields while still loading version 1 input snapshots.
- Real local R-Type/protected/vertical smoke passed with the configured local corpus.
- Remaining: artifact-proof closure for the full roster, plus authenticity work for video priority, raster timing, DIP behavior, and board-specific timing.

### Irem M81

- Checked-in manifests for `dbreed`, `hharry`, and `xmultipl`.
- Explicit V30/Z80/YM2151/DAC/8259 first-pass board assembly in `src/manifests/irem_m81`.
- Player adapter in `src/apps/player/adapters/irem_m81`.
- CLI/system-family routing via `--system irem_m81` and alias `m81`.
- Save-state identity across M81 board-layout profiles, capability discovery, rollback-ready save-state reporting, and real local data-gated smoke through `MNEMOS_M81_SET_DIR=D:\emu\irem\M81`.
- The M81 palette window now uses the shared KNA91-style CPU-visible contract: low-byte-only 5-bit writes, high-byte open bus, and disconnected-A9 mirrors, while the renderer keeps canonical R/G/B plane storage.
- The M81 adapter now consumes explicit arcade `service` and `test` frontend inputs, keeps `mode` as a legacy service alias, maps operator test to the board-visible system bit 6, and persists those fields in adapter state version 2.

### Irem M82

- Player-routable R-Type II profile via `--system irem_m82`.
- Wrapper ZIP/unpacked-folder handling, clone parent fallback, save/load, capability reporting, and real R-Type II data-gated smoke.
- The M82 palette window now uses the shared KNA91-style CPU-visible contract already proven for M72: low-byte-only 5-bit writes, high-byte open bus, and disconnected-A9 mirrors, while the renderer keeps canonical R/G/B plane storage.
- The M82 adapter now consumes explicit arcade `service` and `test` frontend inputs, keeps `mode` as a legacy service alias, maps operator test to the board-visible system bit 6, and persists those fields in adapter state version 2.

### Irem M84

- Checked-in manifests for `hharryb` and `hharryu`.
- M84-owned executable wrapper in `src/manifests/irem_m84`.
- The current executable M84 slice uses the M81-compatible V30/Z80/YM2151/DAC board core for local Hammerin' Harry split sets while preserving separate M84 manifest and save-state identity.
- Player adapter added at `src/apps/player/adapters/irem_m84`.
- CLI/system-family routing via `--system irem_m84` and alias `m84`.
- Clone-parent media routing composes M84 child media with supplemental M81 `hharry` parent media.
- Capability discovery, rollback-ready save-state reporting, and real local player smoke are data-gated through `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81`.
- The current M84 compatibility core exposes the same KNA91-style palette-bus contract through its owned M81 board while preserving M84 manifest/save identity.
- The M84 adapter now consumes explicit arcade `service` and `test` frontend inputs, keeps `mode` as a legacy service alias, maps operator test to the board-visible system bit 6, and persists those fields in adapter state version 2.
- Remaining: replace or verify the compatibility-core assumptions with board evidence for M84 memory/I/O behavior, Hammerin' Harry video/priority, raster timing, DIP behavior, and screenshot parity before calling this authentic.

### Irem M107

- M107 now has a first-pass executable board, not only ROM-contract metadata.
- Board implementation lives in `src/manifests/irem_m107/m107_system.hpp` and `src/manifests/irem_m107/m107_system.cpp`.
- The board owns a main V30, sound V30, M107 video diagnostic path, YM2151, Irem/Nanao GA20 PCM, 20-bit little-endian main/sound buses, RAM windows, I/O ports, frame stepping, and whole-board save/load with identity checks.
- Checked-in manifests and tests cover the local M107 sets currently embedded in the corpus gate, including `airass` and `firebarr`.
- Player adapter lives in `src/apps/player/adapters/irem_m107`.
- CLI/system-family routing is available through `--system irem_m107` and alias `m107`.
- Adapter accepts direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml`, and raw synthetic maincpu fallback.
- Capability discovery reports M107 memory views, V30 trace surfaces, YM2151/GA20 chip registers, rollback-ready save-state, and `media.rom_set state=available` for valid corpus media.
- Real local Air Assault player smoke wrote nonblank screenshots and successfully saved/loaded state.
- The previous OKI6295 placeholder has been replaced by a native GA20 PCM model, and the M107 player now captures GA20 PCM at the YM output cadence and mixes drained GA20 stereo samples into the player audio buffer with signed clamping.
- The M107 adapter now consumes explicit arcade `service` frontend input for the currently modeled service bit, keeps `mode` as a legacy service alias, and persists explicit `service` / `test` fields in adapter state version 2. Operator-test board wiring remains unassigned until the M107 input map is verified.
- Remaining: this is still first-pass diagnostic rendering and executable wiring. Authentic M107 closure still needs V33/config proof if applicable, exact M107 memory/I/O map, GA21/GA22 video/priority behavior, remaining GA20 sound-CPU protocol plus analog balance/filtering proof, DIP behavior, raster timing, and screenshot parity.

### Irem M15

- M15 now has a first-pass executable board and player adapter for `headoni`, not only ROM-contract metadata.
- Board implementation lives in `src/manifests/irem_m15/m15_system.hpp` and `src/manifests/irem_m15/m15_system.cpp`.
- The board owns a MOS 6502 execution path via the shared `m6510` core in bare-6502 mode, M15 tile/color/chargen video path, 1-bit beeper, scratch/video/color/chargen RAM windows, input/DIP/control MMIO, frame IRQ pulse, and whole-board save/load with identity checks.
- M15 sound writes to `$a100` now retain board-owned discrete latch evidence: total write count, per-bit rise/fall counters, the active-low bit-6 speaker output, and speaker output edge count, with board and adapter save-state coverage.
- The M15 map is now aligned with MAME/source metadata for Head On: scratch RAM `$0000-$02ff`, program ROM `$1000-$33ff`, vector ROM `$fc00-$ffff`, video RAM `$4000-$43ff`, color RAM `$4800-$4bff`, chargen RAM `$5000-$57ff`, P2 read `$a000`, sound write `$a100`, DIP read `$a200`, P1 read `$a300`, and control write `$a400`.
- The checked-in `headoni` manifest now uses full 64 KiB CPU address space and reloads `e4.9d` at `$fc00` for the 6502 reset/IRQ vectors.
- M15 inputs now follow Head On's active-high P1/P2 ports, `0x11` DIP default, and coin-triggered NMI edge; the active-low control flip bit is part of board state/save state.
- M15 video now renders from video/color/chargen RAM using the M-15 tile scan order and 1bpp palette lookup. Frame stepping is scanline-paced: visible lines compose before the CPU slice for that beam line, and the frame IRQ can change color/video state for later scanlines without repainting earlier ones. The old program-ROM diagnostic fallback was removed.
- Player adapter lives in `src/apps/player/adapters/irem_m15`.
- CLI/system-family routing is available through `--system irem_m15` and alias `m15`.
- Adapter accepts direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml`, and raw synthetic maincpu fallback.
- Capability discovery reports 6502 trace/register surfaces, scratch/video/color/chargen RAM views, rollback-ready save-state, and `media.rom_set state=available` for valid corpus media.
- Real local Head On player smoke wrote nonblank screenshots and successfully saved/loaded state.
- Remaining: authentic M15 closure still needs board-evidenced discrete sample mappings/analog sound behavior, analog color proof, exact raster phase proof, and screenshot parity.

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
- `docs\architecture\factsheets\irem-system-boards-reference.md` records the local Irem M-series board-family reference; `docs\architecture\README.md` links it so M72/M81/M82/M84/M107 work stays classified by lineage and shared custom silicon.
- `D:\emu\irem\imgfight.zip`, `D:\emu\irem\imgfightj.zip`, `D:\emu\irem\imgfightj.7z`, `D:\emu\irem\imgfightjb.zip`, and `D:\emu\irem\imgfightjb.7z` are local corpus inputs for sorting Image Fight parent/clone composition.
- `scripts\irem\inventory-corpus.ps1` now emits per-item `tracked_family`, `manifest_parent`, `set_role`, `archive_composition`, and `load_readiness` fields plus a grouped `tracked_sets` section. Current local Image Fight grouping: `imgfight` is the M72 parent/standalone set with two direct player-loadable ZIP/folder routes plus one metadata-only `.7z`; `imgfightj` and `imgfightjb` are M72 clones declaring parent `imgfight`, each with one direct player-loadable ZIP plus one metadata-only `.7z`.
- `D:\emu\irem\m72` has moved/expanded M72 artifacts, including `gallop.zip`, an unpacked `gallop\`, and an unpacked `nspirit\`.
- `scripts\irem_m72\find-missing-artifacts.ps1 -Root D:\emu\irem -Recurse -Set gallopm72,nspirit` now accepts the comma-separated set list and finds `42/44` artifacts present for those two sets. Missing entries include suggested local placement paths.
- `gallopm72` is incomplete locally: missing `mcu/cc_c-pr-.ic1`, size `0x1000`, CRC `0xac4421b1`.
- World `nspirit` is incomplete locally: missing `mcu/nin_c-pr-b.ic1`, size `0x1000`, CRC `0x0f7b2713`.
- Current scan of `D:\emu\irem\M72\nspirit.zip` finds `23/24` World `nspirit` artifacts and still misses only `nin_c-pr-b.ic1` (`0x0f7b2713`). The same ZIP contains `nspiritj/nin_c-pr-.ic1` (`0x802d440a`), which is the Japan MCU and does not prove World `nspirit`.
- Current exact-path scan of `D:\emu\irem\M72\nspirit.zip`, `D:\emu\irem\M72\gallopm72.zip`, and `D:\emu\irem\M72\gallop.zip` still finds `42/44` artifacts present and still misses only `gallopm72:mcu:cc_c-pr-.ic1` (`0xac4421b1`) and `nspirit:mcu:nin_c-pr-b.ic1` (`0x0f7b2713`). Direct ZIP inspection shows the Ninja Spirit ZIP's only MCU-like entry is the Japan `nspiritj/nin_c-pr-.ic1`; the Gallop ZIPs have PROM/sprite/voice entries but no matching MCU dump.
- If lawful dumps become available, the scanner points at these unpacked destinations: `D:\emu\irem\m72\gallop\cc_c-pr-.ic1` and `D:\emu\irem\m72\nspirit\nin_c-pr-b.ic1`. Equivalent ZIP entries are also valid if the matching set ZIP is rebuilt with the same filenames and CRCs.
- `nspiritj` is a valid Japan variant and has `nin_c-pr-.ic1`, CRC `0x802d440a`; do not use it as proof for World `nspirit`.
- Follow-up recursive zip/nested-zip scan across `D:\emu\irem` found only `D:\emu\irem\Ninja-Spirit_Arcade_JA.zip!nspiritj.zip!nin_c-pr-.ic1`, size `0x1000`, CRC `0x802d440a`; it did not find `gallopm72:mcu:cc_c-pr-.ic1:0xac4421b1` or `nspirit:mcu:nin_c-pr-b.ic1:0x0f7b2713`.
- M82 validation now supports broad `MNEMOS_M82_SET_DIR=D:\emu\irem` again. The M82 manifest/player data gates rank single-inner wrapper ZIPs before direct set ZIPs and unpacked folders, so complete local R-Type II collection wrappers win over incomplete earlier-sorted candidates such as `D:\emu\irem\M72\rtype2`.
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

M107 GA20 player-mixer continuation validation on 2026-06-26:

- Exact M72 archive scan: `scripts\irem_m72\find-missing-artifacts.ps1 -Root D:\emu\irem\M72 -Recurse -Set 'gallopm72,nspirit'` found `42/44` artifacts present and still missed only `gallopm72:mcu:cc_c-pr-.ic1` plus `nspirit:mcu:nin_c-pr-b.ic1`.
- Direct ZIP inspection: `D:\emu\irem\M72\nspirit.zip` contains `nspiritj/nin_c-pr-.ic1` only under MCU-like entries; `D:\emu\irem\M72\gallopm72.zip` contains PROM entries but no `cc_c-pr-.ic1`; `D:\emu\irem\M72\gallop.zip` contains sprite/voice entries but no `cc_c-pr-.ic1`.
- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_chips_audio_irem_ga20_test mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test`
- Focused CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `4/4` passed for `mnemos_chips_audio_irem_ga20_test`, `mnemos_manifests_irem_m107_test`, `mnemos_apps_player_irem_m107_adapter_test`, and `mnemos_apps_player_irem_m107_corpus_golden_test`
- `git diff --check`: clean, with only existing LF-to-CRLF working-copy warnings.
- Full build: `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, broad-root M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `189/189`, with expected conformance/media skips and the expected M72 roster skip.

Continuation validation on 2026-06-25 21:19 -05:00:

- `scripts\irem_m72\find-missing-artifacts.ps1 -Root D:\emu\irem -Recurse -Set gallopm72,nspirit`: `42/44` present, missing only `cc_c-pr-.ic1` and `nin_c-pr-b.ic1`, with suggested placements under `D:\emu\irem\m72\gallop\` and `D:\emu\irem\m72\nspirit\`
- Same artifact command with `-Set 'gallopm72,nspirit'` validates the comma-separated set-list path
- `scripts\irem_m72\run-corpus-smoke.ps1 -Rom D:\emu\irem\imgfight.zip,D:\emu\irem\m72\imgfight -MaxSets 1`: `1/1` passed
- `scripts\irem_m72\run-corpus-smoke.ps1 -Rom D:\emu\irem\m72\gallop.zip,D:\emu\irem\m72\gallop -MaxSets 1`: expected failure with `media_clean=False`, lit screenshot, and missing `cc_c-pr-.ic1`

M72 no-dump MCU checksum-response continuation validation on 2026-06-26:

- `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m72_test`
- `ctest --preset windows-msvc-debug --output-on-failure -R mnemos_manifests_irem_m72_test`: `1/1` passed

M107 GA20 continuation validation on 2026-06-26:

- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_chips_audio_irem_ga20_test mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test`
- Focused CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `4/4` passed for `mnemos_chips_audio_irem_ga20_test`, `mnemos_manifests_irem_m107_test`, `mnemos_apps_player_irem_m107_adapter_test`, and `mnemos_apps_player_irem_m107_corpus_golden_test`
- Full preset build: `cmake --build --preset windows-msvc-debug`
- Full preset CTest with local Irem env vars and `MNEMOS_M72_SET_DIR` intentionally cleared: `189/189`, with expected data-gated skips including the M72 roster golden
- Direct M107 capability smoke: `mnemos_player --system irem_m107 --rom D:\emu\irem\M107\airass.zip --capabilities` reported `audio.ga20.samples`, `memory.ga20.registers`, `memory.ym2151.registers`, V30 trace surfaces, and `media.rom_set state=available`
- Current `D:\emu\irem\M72\nspirit.zip` artifact scan: `23/24` World `nspirit` artifacts present; still missing `nin_c-pr-b.ic1` (`0x0f7b2713`). The ZIP contains `nspiritj/nin_c-pr-.ic1` (`0x802d440a`), which remains Japan-only evidence.
- Full preset build and CTest: `188/188` passed; ROM-gated corpus tests skipped where env vars were unset
- `scripts\irem_m72\run-corpus-smoke.ps1 -Rom D:\emu\irem\m72\dbreedm72,D:\emu\irem\m72\dkgensanm72 -Frames 600`: `2/2` passed, summary `build\scratch\irem-m72-corpus\20260625-231315\summary.json`

M72 palette-bus continuation validation on 2026-06-26:

- Focused build/test: `mnemos_manifests_irem_m72_test` passed
- Focused M72 adjacent CTest sweep passed: `mnemos_chips_video_irem_m72_video_test`, `mnemos_manifests_irem_m72_test`, and `mnemos_apps_player_irem_m72_adapter_test`
- Full preset build and CTest: `188/188` passed; ROM-gated corpus tests skipped where env vars were unset
- `scripts\irem_m72\run-corpus-smoke.ps1 -Rom D:\emu\irem\R-Type_Arcade_EN.zip,D:\emu\irem\imgfight.zip -Frames 600`: `2/2` passed, summary `build\scratch\irem-m72-corpus\20260625-233344\summary.json`

M72 cabinet input continuation validation on 2026-06-26:

- `git diff --check`: clean, with only existing CRLF conversion warnings
- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_apps_player_irem_m72_adapter_test mnemos_manifests_irem_m72_test`
- Focused CTest: `ctest --preset windows-msvc-debug --output-on-failure -R "mnemos_(apps_player_irem_m72_adapter|manifests_irem_m72)_test"`: `2/2` passed
- Full preset build and CTest: `188/188` passed; ROM-gated corpus tests skipped where env vars were unset
- `scripts\irem_m72\run-corpus-smoke.ps1 -Rom D:\emu\irem\R-Type_Arcade_EN.zip,D:\emu\irem\imgfight.zip -Frames 600`: `2/2` passed, summary `build\scratch\irem-m72-corpus\20260625-234550\summary.json`

M81/M82/M84 cabinet input continuation validation on 2026-06-26:

- `git diff --check`: clean, with only existing CRLF conversion warnings
- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_apps_player_irem_m81_adapter_test mnemos_apps_player_irem_m82_adapter_test mnemos_apps_player_irem_m84_adapter_test`
- Focused CTest: `ctest --preset windows-msvc-debug --output-on-failure -R "mnemos_apps_player_irem_m(81|82|84)_adapter_test"`: `3/3` passed
- Full preset build and CTest: `188/188` passed; ROM-gated corpus tests skipped where env vars were unset
- Local corpus CTest with `MNEMOS_M81_SET_DIR=D:\emu\irem\M81`, `MNEMOS_M82_SET_DIR=D:\emu\irem`, and `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81`: `3/3` passed for `mnemos_apps_player_irem_m81_corpus_golden_test`, `mnemos_apps_player_irem_m82_rtype2_golden_test`, and `mnemos_apps_player_irem_m84_corpus_golden_test`

M107 cabinet service input continuation validation on 2026-06-26:

- `git diff --check`: clean, with only existing CRLF conversion warnings
- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_apps_player_irem_m107_adapter_test`
- Focused CTest: `ctest --preset windows-msvc-debug --output-on-failure -R mnemos_apps_player_irem_m107_adapter_test`: `1/1` passed
- Full preset build and CTest: `188/188` passed; ROM-gated corpus tests skipped where env vars were unset
- Local corpus CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `2/2` passed for `mnemos_manifests_irem_m107_test` and `mnemos_apps_player_irem_m107_corpus_golden_test`

Irem corpus inventory grouping validation on 2026-06-26:

- `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-corpus\inventory-after.json`
- Report summary: `121` items, `58` tracked by checked-in Irem manifests, `52` directly player-loadable, and `6` tracked metadata-only `.7z` artifacts that need unpacking or ZIP conversion before player load.
- `tracked_sets` now separates Image Fight parent/clone readiness: `imgfight` direct-loadable `2` / metadata-only `1`, `imgfightj` parent `imgfight` direct-loadable `1` / metadata-only `1`, and `imgfightjb` parent `imgfight` direct-loadable `1` / metadata-only `1`.

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
  - output reported `media.rom_set state=available`, rollback available, V30 trace surfaces, YM2151/GA20 registers, and M107 RAM views
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

M15 sound-latch continuation validation on 2026-06-26:

- `git diff --check`: clean, with only existing CRLF conversion warnings
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m15_test mnemos_apps_player_irem_m15_adapter_test`
- Focused CTest with `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`: `2/2`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_irem_m15_adapter_test`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `188/188`, with the expected M72 roster and non-Irem media/conformance skips

M15 scanline-paced video continuation validation on 2026-06-26:

- `git diff --check`: clean, with only existing CRLF conversion warnings
- Focused M15 build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m15_test mnemos_apps_player_irem_m15_adapter_test`
- Focused M15 CTest with `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`: `2/2`
  - `mnemos_manifests_irem_m15_test`
  - `mnemos_apps_player_irem_m15_adapter_test`
- Focused M82 corpus rerun with exact wrapper files: `2/2`
  - `mnemos_manifests_irem_m82_test`
  - `mnemos_apps_player_irem_m82_rtype2_golden_test`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, exact-file M82 wrappers, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `188/188`, with the expected M72 roster and non-Irem media/conformance skips

M82 broad-corpus selection continuation validation on 2026-06-26:

- `git diff --check`: clean, with only existing CRLF conversion warnings
- Focused M82 build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m82_test mnemos_apps_player_irem_m82_adapter_test`
- Focused M82 CTest with `MNEMOS_M82_SET_DIR=D:\emu\irem`: `2/2`
  - `mnemos_manifests_irem_m82_test`
  - `mnemos_apps_player_irem_m82_rtype2_golden_test`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, broad-root M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `188/188`, with the expected M72 roster and non-Irem media/conformance skips

M82 KNA91 palette-bus continuation validation on 2026-06-26:

- Imported the Irem M-series fact sheet into `docs\architecture\factsheets\irem-system-boards-reference.md` and linked it from `docs\architecture\README.md`.
- `git diff --check`: clean, with only existing CRLF conversion warnings.
- Focused M82 build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m82_test`
- Focused M82 CTest with `MNEMOS_M82_SET_DIR=D:\emu\irem`: `1/1`
  - `mnemos_manifests_irem_m82_test`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, broad-root M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `188/188`, with the expected M72 roster and non-Irem media/conformance skips.

M81/M84 KNA91 palette-bus continuation validation on 2026-06-26:

- Extended the shared KNA91 CPU-visible palette-bus behavior to the M81 board core and verified the current M84 compatibility wrapper exposes the same bus semantics through its owned M81 board.
- `git diff --check`: clean, with only existing CRLF conversion warnings.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m81_test mnemos_manifests_irem_m84_test`
- Focused CTest with `MNEMOS_M81_SET_DIR=D:\emu\irem\M81` and `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81`:
  - `2/2`
  - `mnemos_manifests_irem_m81_test`
  - `mnemos_manifests_irem_m84_test`
- Full build / CTest:
  - `cmake --build --preset windows-msvc-debug`
  - Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, broad-root M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `188/188`, with the expected M72 roster and non-Irem media/conformance skips.

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

1. Continue M15 authenticity work: board-evidenced discrete sample mappings/analog sound behavior, analog color proof, exact raster phase proof, and screenshot parity.
2. Continue M107 authenticity work: exact board clocks, V33/V30 facts, M107 memory/I/O map, sound CPU protocol, GA20 analog balance/filtering, GA21/GA22 behavior, DIP behavior, raster timing, and screenshot parity.
3. Do the M84 authenticity pass and replace or validate the M81-compatible assumptions.
4. Continue M72 artifact closure by locating exact Gallop and World Ninja Spirit MCU dumps. Do not substitute Japan `nspiritj` or synthetic fill bytes.
5. Continue authenticity passes for M81/M82/M72 video priority, raster phase/timing, DIP behavior, M81/M82 palette-bank rendering/decode, and board timing.

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
