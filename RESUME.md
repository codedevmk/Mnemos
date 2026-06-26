# Irem Arcade Feature Handoff

Date: 2026-06-26

## Resume Point

- Worktree: `C:\dev\emu\Mnemos-irem-arcade`
- Branch: `feature/irem-arcade`
- Remote: `origin` -> `https://github.com/codedevmk/Mnemos.git`
- Resume from: `origin/feature/irem-arcade` after the 2026-06-26 M52 sound-CPU ownership continuation commit and push.
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

### Irem M52

- M52 now has a first-pass executable Moon Patrol board and player adapter, not only corpus metadata.
- Board implementation lives in `src/manifests/irem_m52/m52_system.hpp` and `src/manifests/irem_m52/m52_system.cpp`.
- Checked-in manifests cover `mpatrol` and `mpatrolw`; the Williams clone manifest declares parent `mpatrol` and includes same-region parent-shared sound/PROM declarations so parent fallback can compose cleanly.
- The parent manifest carries 13 Moon Patrol Instruction Manual active-high SW1/SW2 DIP definitions; the Williams clone inherits them.
- Player adapter lives in `src/apps/player/adapters/irem_m52` and supports direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml`, supplemental parent media, and raw synthetic maincpu fallback.
- CLI/system-family routing is available through `--system irem_m52` and alias `m52`.
- The board owns two native YM2149/AY-compatible SSG instances and one native OKI MSM5205 decoder instead of the earlier one-bit audio probe. Sound-command writes now only update the command latch and IRQ line; AY/MSM state is mutated through sound-Z80 port writes, and the adapter mixes all captured stereo queues for player audio.
- The board also owns and schedules a second Z80 sound CPU with mapped `soundcpu` ROM/RAM, sound-command latch IRQ/ack state, modeled AY/MSM port writes, and save/load coverage for both Z80s, sound RAM, latch state, and audio chip phases.
- The M52 adapter consumes explicit arcade `service` and `test` frontend inputs: service maps active-low to system bit `0x08`, `mode` remains a legacy service alias, operator-test maps active-low to bit `0x10`, and adapter save-state version 1 persists those fields.
- The M52 adapter retains DIP metadata, folds manual-backed factory defaults to `dsw1=0x01` / `dsw2=0x02`, exposes `DIP switches=13`, still lets explicit `--dip` override raw bytes, and uses board/save-target revision 6 for the corrected sound-ownership/state identity.
- Capability discovery reports main/sound Z80 trace/register surfaces, both YM2149 register snapshots, MSM5205 register snapshots, M52 RAM views, rollback-ready save-state, and `media.rom_set state=available` for valid corpus media.
- Real local Moon Patrol wrapper ZIPs load through the data-gated corpus test and direct player screenshot smoke.
- Remaining: this is still first-pass diagnostic rendering and partial sound-protocol timing. Authentic M52 closure still needs true Moon Patrol background/road/sprite priority, exact sound CPU port/protocol proof, MSM5205 stream timing, discrete sound behavior, exact raster timing, runtime DIP behavior beyond current manual defaults, and screenshot/audio parity before it is counted as correct graphics/music.

### Irem M72

- First-pass playable profile with checked-in true-M72 manifests.
- Protected-set handling, MCS-51/i8751 plumbing, selected no-dump HLE declarations, parent/clone loader hardening, media validation reporting, vertical/R-Type/protected gates, and corpus smoke tooling.
- `dbreedm72` and `dkgensanm72` no-dump MCU HLE profiles now cover startup inversion, entry continuation stubs, command-latch acknowledge, sample-trigger cursor setup, and profile-specific service-ROM checksum response bytes.
- Collection ZIPs with checked-in M72 top-level directories now prefer the canonical M72-suffixed set when the source stem is plain, so `D:\emu\irem\M72\airduel.zip` resolves as `airduelm72` instead of being pulled toward the Japan clone by shared CRC hits. The same direct probe still reports missing Air Duel graphics/sample entries, so it is not clean roster proof.
- Palette RAM now exposes the M72 CPU-visible disconnected-A9 mirror and low-byte-only 5-bit gun behavior while keeping canonical R/G/B plane storage for rendering and save states.
- The M72 player adapter now consumes explicit arcade `service` and `test` frontend inputs: service 1/2 map active-low to system bits 4/5, `mode` remains a legacy service alias, operator test maps active-low to bit 6, and adapter save-state version 2 persists those fields while still loading version 1 input snapshots.
- Real local R-Type/protected/vertical smoke passed with the configured local corpus.
- Remaining: artifact-proof closure for the full roster, plus authenticity work for video priority, raster timing, DIP behavior, and board-specific timing.

### Irem M75

- M75 now has a first-pass executable Vigilante board and player adapter, not only corpus metadata.
- Board implementation lives in `src/manifests/irem_m75/m75_system.hpp` and `src/manifests/irem_m75/m75_system.cpp`.
- The current M75 route is intentionally Z80 main + Z80 sound + YM2151 + DAC for Vigilante, not the V30-family M-series core used by later Irem boards.
- Checked-in manifest coverage includes parent set `vigilant` plus official regional clones `vigilanta`, `vigilantb`, `vigilantc`, `vigilantd`, `vigilantg`, and `vigilanto`. The parent comes from the local complete single-inner wrapper `D:\emu\irem\Vigilante_Arcade_EN (3).zip`; clone wrappers carry partial regional deltas and now resolve shared media through the parent fallback path.
- The parent manifest carries 14 Vigilante Installation & Service Manual SW1/SW2 DIP definitions; clones inherit them through the parent manifest path.
- The board owns fixed/banked main ROM mapping, sound latch/IRQ/ack state, YM2151 and DAC ports, sample-ROM reads, M75 RAM windows, two-bank 5-bit KNA91-style palette writes/readback, rear color/disable register behavior, palette/video/sprite RAM, frame stepping, audio draining, and whole-board save/load identity. A synthetic sound-Z80 proof now verifies the sample-address ports, consecutive sample ROM reads, DAC writes, and latch acknowledge path.
- Player adapter lives in `src/apps/player/adapters/irem_m75`.
- CLI/system-family routing is available through `--system irem_m75` and alias `m75`.
- Adapter accepts direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml`, clone/parent fallback media resolution, resident CRC media reporting, rollback-ready save-state, capability discovery, and raw synthetic maincpu fallback.
- The M75 adapter now consumes explicit arcade `service` and `test` frontend inputs: service maps active-low to system bit `0x10`, `mode` remains a legacy service alias, operator-test maps active-low to bit `0x20`, and adapter save-state version 2 persists those fields.
- The M75 adapter retains DIP metadata, folds active-low manifest defaults to `dsw1=0xff` / `dsw2=0xfd`, exposes `DIP switches=14`, and still lets explicit `--dip` override the raw bytes.
- Real local Vigilante parent and official regional clone wrapper ZIPs load through the data-gated corpus test; the Japan clone also has direct player screenshot/save-state smoke evidence.
- Remaining: this is still first-pass diagnostic rendering and executable wiring. Authentic M75 closure still needs Vigilante tile/sprite/rear-layer priority, exact scroll behavior, DIP runtime behavior beyond current manual defaults, reference-backed Z80 sound protocol/sample timing, raster timing, bootleg coverage, and screenshot/audio parity before it is counted as correct graphics/music.

### Irem M81

- Checked-in manifests for `dbreed`, `hharry`, and `xmultipl`.
- Explicit V30/Z80/YM2151/DAC/8259 first-pass board assembly in `src/manifests/irem_m81`.
- Player adapter in `src/apps/player/adapters/irem_m81`.
- CLI/system-family routing via `--system irem_m81` and alias `m81`.
- Save-state identity across M81 board-layout profiles, capability discovery, rollback-ready save-state reporting, and real local data-gated smoke through `MNEMOS_M81_SET_DIR=D:\emu\irem\M81`.
- The M81 palette window now uses the shared KNA91-style CPU-visible contract: low-byte-only 5-bit writes, high-byte open bus, and disconnected-A9 mirrors, while the renderer keeps canonical R/G/B plane storage.
- The M81 adapter now consumes explicit arcade `service` and `test` frontend inputs, keeps `mode` as a legacy service alias, maps operator test to the board-visible system bit 6, and persists those fields in adapter state version 2.

### Irem M82

- Player-routable M82 profile via `--system irem_m82` for Major Title and R-Type II local routes.
- Wrapper ZIP/unpacked-folder handling, clone parent fallback, save/load, capability reporting, and real M82 data-gated smoke.
- Checked-in manifests now cover `majtitle`, `majtitlej`, `rtype2`, `rtype2j`, `rtype2jc`, and `rtype2m82b`; the Major Title parent declares the additional `backgrounds` graphics ROM region, and the M82 renderer now uses that region as the rear tilemap graphics source when present while falling back to the normal `tiles` region for sets without dedicated background graphics.
- The M82 palette window now uses the shared KNA91-style CPU-visible contract already proven for M72: low-byte-only 5-bit writes, high-byte open bus, and disconnected-A9 mirrors, while the renderer keeps canonical R/G/B plane storage.
- The M82 adapter now consumes explicit arcade `service` and `test` frontend inputs, keeps `mode` as a legacy service alias, maps operator test to the board-visible system bit 6, and persists those fields in adapter state version 2.

### Irem M84

- Checked-in manifests for `gallop`, `hharryb`, `hharryu`, and `ltswords`.
- M84-owned executable wrapper in `src/manifests/irem_m84`.
- The current executable M84 slice uses the M81-compatible Z80/YM2151/DAC/KNA91-style board core for local Hammerin' Harry split sets plus the standalone local `ltswords` folder and `gallop.zip` archive while preserving separate M84 manifest and save-state identity.
- Hammerin' Harry M84 profiles select V30; `ltswords` and `gallop` select V35 and reject save-state restore under the wrong M84 CPU/layout identity.
- `ltswords` loads the CRC-verified program, sound, graphics, and sample ROMs from `D:\emu\irem\M72\ltswords`, but the small PROM/PLD artifacts remain missing and are explicitly declared through `irem_m84_prom_pld` HLE metadata rather than treated as authentic video proof.
- `gallop` loads the complete CRC-verified local M84 Gallop / Cosmic Cop archive from `D:\emu\irem\M72\gallop.zip`, including program, sound, graphics, samples, PROMs, and PLDs. The same `M72` bucket also contains a misleading `D:\emu\irem\M72\gallop` unpacked folder with different true-M72-style filenames; M84 corpus tests now prefer the complete ZIP when both are present.
- `gallop` now carries 10 manifest DIP switch definitions. The M84 adapter retains that metadata, applies the composed manifest default (`0xf9bf`) to the board DIP register, and exposes the switch count in the player system spec.
- Player adapter added at `src/apps/player/adapters/irem_m84`.
- CLI/system-family routing via `--system irem_m84` and alias `m84`.
- Clone-parent media routing composes M84 child media with supplemental M81 `hharry` parent media when a set declares `parent`; standalone M84 folders load directly.
- Capability discovery, rollback-ready save-state reporting, and real local player smoke are data-gated through `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72`.
- The current M84 compatibility core exposes the same KNA91-style palette-bus contract through its owned M81 board while preserving M84 manifest/save identity.
- The M84 adapter now consumes explicit arcade `service` and `test` frontend inputs, keeps `mode` as a legacy service alias, maps operator test to the board-visible system bit 6, and persists those fields in adapter state version 2.
- Remaining: replace or verify the compatibility-core assumptions with board evidence for M84 memory/I/O behavior, Hammerin' Harry/Cosmic Cop/Ken-Go video/priority, raster timing, board-authentic DIP behavior, recover/prove the `ltswords` PROM/PLD artifacts, and screenshot/audio parity before calling this authentic.

### Irem M92

- Checked-in manifests now cover `bmaster`, `gunforce`, `gunforcej`, `gunforceu`, `gunforc2`, `gunhohki`, `hook`, `inthunt`, `mysticri`, `mysticrib`, `nbbatman`, and `nbbatmanu`.
- `gunforcej` and `gunforceu` are local split clone sets declaring parent `gunforce`; their manifests carry only the changed main CPU ROMs and inherit shared sound, tile, sprite, sample, and PLD regions from the parent.
- `gunhohki` and `mysticrib` are local Mystic Riders family split clone routes declaring parent `mysticri`; `gunhohki` carries changed main CPU ROMs, while `mysticrib` carries changed main and sound CPU ROMs and inherits graphics, samples, and PLDs from the parent.
- `nbbatman` is the complete local Ninja Baseball Bat Man parent wrapper route; `nbbatmanu` declares parent `nbbatman` and carries the changed lower main-program pair plus parent-fallback declarations for the unchanged upper main-program pair.
- The M92 adapter now resolves clone parents beside the selected set path via direct `parent.zip`, unpacked parent directories, or sibling single-inner wrapper ZIPs such as `D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_EN.zip`.
- Player adapter remains first-pass: NEC V33/V35 shell, YM2151/GA20 windows, diagnostic GA21/GA22-era video path, resident media validation, rollback-ready save-state, and capability discovery.
- The M92 sound-command latch now tracks pending command/reply state, clears the command-pending bit when the V35 reads the latch, preserves those bits in board save state version 2, and has synthetic V33-to-V35 command/reply proof through the sound latch and reply port.
- `MNEMOS_M92_SET_DIR=D:\emu\irem` now data-gates twelve M92 sets. Direct `mnemos_player` smokes for `gunforceu`, `gunforcej`, `mysticri`, `gunhohki`, `mysticrib`, `nbbatman`, and `nbbatmanu` write 320x240 nonblank PPMs plus save states after one frame.
- Remaining: encrypted V35 sound execution/decryption/IRQ timing proof, exact M92 memory/I/O maps, GA21/GA22 video and priority behavior, GA20 analog balance/filtering, DIP/raster behavior, protection details, and screenshot/audio parity before calling M92 authentic.

### Irem M107

- M107 now has a first-pass executable board, not only ROM-contract metadata.
- Board implementation lives in `src/manifests/irem_m107/m107_system.hpp` and `src/manifests/irem_m107/m107_system.cpp`.
- The board owns a main V-series CPU configured as NEC V33 at 14 MHz, a sound V-series CPU configured as NEC V35 at 14.318181 MHz, M107 video diagnostic path, YM2151, Irem/Nanao GA20 PCM, 20-bit little-endian main/sound buses, board-evidenced main/sound RAM and MMIO windows, I/O ports, frame stepping, and whole-board save/load with identity checks.
- M107 board save-state identity now includes the CPU model and clock semantics, so states from the previous V30/half-rate sound-clock contract are rejected by the board revision instead of loading silently.
- Checked-in manifests and tests cover the local M107 sets currently embedded in the corpus gate, including `airass` and `firebarr`.
- Player adapter lives in `src/apps/player/adapters/irem_m107`.
- CLI/system-family routing is available through `--system irem_m107` and alias `m107`.
- Adapter accepts direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml`, and raw synthetic maincpu fallback.
- Capability discovery reports M107 memory views, V30 trace surfaces, YM2151/GA20 chip registers, rollback-ready save-state, and `media.rom_set state=available` for valid corpus media. The false `shared_ram` memory view was removed; current views are `work_ram`, `sprite_ram`, `palette_ram`, `vram`, and `sound_ram`.
- Real local Air Assault player smoke wrote nonblank screenshots and successfully saved/loaded state.
- The previous OKI6295 placeholder has been replaced by a native GA20 PCM model, and the M107 player now captures GA20 PCM at the YM output cadence and mixes drained GA20 stereo samples into the player audio buffer with signed clamping.
- The M107 adapter now consumes explicit arcade `service` and `test` frontend inputs: service maps active-low to the `COINS_DSW3` service-credit bit `0x10`, `mode` remains a legacy service alias, operator test maps active-low to the `COINS_DSW3` operator-service bit `0x20`, and adapter state version 2 persists those fields while still loading version 1 snapshots.
- The M107 sound-command latch now asserts the first-pass V35 command IRQ line through INTP1/vector 25 on main V33 writes; sound-side reads fetch the command without acknowledging it, sound-side writes to `$a8044` acknowledge/clear the command IRQ, YM2151 Timer A IRQ dispatches through V35 INTP0/vector 24, simultaneous pending YM/command IRQs prefer the modeled INTP0/vector 24 path before INTP1/vector 25, a still-pending command IRQ is serviced through INTP1 after the YM2151 source is cleared, and command/reply state is preserved in board save state version 8.
- The M107 map now models VRAM at `$d0000`, work RAM at `$e0000`, sprite RAM at `$f8000`, palette RAM at `$f9000`, sound RAM at `$a0000`, and sound-side GA20/YM2151/command-latch/reply MMIO at `$a8000`/`$a8040`/`$a8044`/`$a8046`. Port fallbacks remain for the current synthetic command path.
- Checked-in Air Assault and Fire Barrel manifests now carry the shared SW1/SW2 and SW3 DIP profile from the Fire Barrel input profile. The adapter retains the 12 parsed DIP entries, folds SW1/SW2 defaults into the board DIP word (`0xffbf`), folds SW3 defaults into the separate `COINS_DSW3` word (`0xebff`), and exposes `DIP switches=12` in the player system spec.
- Remaining: this is still first-pass diagnostic rendering and executable wiring. Authentic M107 closure still needs V33/V35-specific timing and on-die peripheral proof beyond the shared V30-compatible core, deeper M107 I/O behavior, GA21/GA22 video/priority behavior, cycle-exact V35 interrupt-latency proof, remaining GA20 analog balance/filtering proof, raster timing, and screenshot parity.

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
$env:MNEMOS_M52_SET_DIR="D:\emu\irem"
$env:MNEMOS_M75_SET_DIR="D:\emu\irem"
$env:MNEMOS_M81_SET_DIR="D:\emu\irem\M81"
$env:MNEMOS_M82_SET_DIR="D:\emu\irem"
$env:MNEMOS_M84_SET_DIR="D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72"
$env:MNEMOS_M107_SET_DIR="D:\emu\irem\M107"
```

Important local corpus facts:

- `D:\emu\irem\imgfightjb.zip` exists. ZIP versus unpacked folder is not the blocker; the loader supports both. The relevant distinction is split, merged, parent, and standalone composition.
- `docs\architecture\factsheets\irem-system-boards-reference.md` records the local Irem M-series board-family reference; `docs\architecture\README.md` links it so M72/M81/M82/M84/M107 work stays classified by lineage and shared custom silicon.
- `D:\emu\irem\imgfight.zip`, `D:\emu\irem\imgfightj.zip`, `D:\emu\irem\imgfightj.7z`, `D:\emu\irem\imgfightjb.zip`, and `D:\emu\irem\imgfightjb.7z` are local corpus inputs for sorting Image Fight parent/clone composition.
- `scripts\irem\inventory-corpus.ps1` now emits per-item `tracked_family`, `manifest_parent`, `set_role`, `archive_composition`, and `load_readiness` fields plus a grouped `tracked_sets` section. Current local Image Fight grouping: `imgfight` is the M72 parent/standalone set with two direct player-loadable ZIP/folder routes plus one metadata-only `.7z`; `imgfightj` and `imgfightjb` are M72 clones declaring parent `imgfight`, each with one direct player-loadable ZIP plus one metadata-only `.7z`.
- M52 Moon Patrol local wrapper routes are `D:\emu\irem\Moon-Patrol_Arcade_EN.zip` for `mpatrol` and `D:\emu\irem\Moon-Patrol_Arcade_EN (1).zip` for `mpatrolw`; both are single-inner ZIP wrappers and directly player-loadable through `MNEMOS_M52_SET_DIR=D:\emu\irem`.
- M75 Vigilante local wrapper route is `D:\emu\irem\Vigilante_Arcade_EN (3).zip`; it is a single-inner ZIP wrapper and directly player-loadable through `MNEMOS_M75_SET_DIR=D:\emu\irem`.
- M92 GunForce local wrapper routes are `D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_EN.zip` for parent `gunforce`, `D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_JA.zip` for clone `gunforcej`, and `D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_EN (1).zip` for clone `gunforceu`. The two clone wrappers are split sets and require parent fallback to the parent wrapper.
- M92 Mystic Riders local wrapper routes are `D:\emu\irem\Mystic-Riders_Arcade_EN.zip` for parent `mysticri`, `D:\emu\irem\Mystic-Riders_Arcade_JA.zip` for clone `gunhohki`, and `D:\emu\irem\Mystic-Riders_Arcade_EN (1).zip` for clone `mysticrib`. The Japan and split bootleg routes require parent fallback to the parent wrapper.
- M92 Ninja Baseball Bat Man local wrapper routes are `D:\emu\irem\Ninja-Baseball-Bat-Man_Arcade_EN.zip` for parent `nbbatman` and `D:\emu\irem\Ninja-Baseball-Bat-Man_Arcade_EN (1).zip` for clone `nbbatmanu`. The US clone wrapper contains only the changed main-program pair and requires parent fallback to the parent wrapper for the unchanged upper program, sound, graphics, samples, and PLDs.
- `D:\emu\irem\m72` has moved/expanded M72 artifacts, including `gallop.zip`, an unpacked `gallop\`, and an unpacked `nspirit\`.
- `scripts\irem_m72\find-missing-artifacts.ps1 -Root D:\emu\irem -Recurse -Set gallopm72,nspirit` now accepts the comma-separated set list and finds `43/44` artifacts present for those two sets.
- `gallopm72` is incomplete locally: missing `mcu/cc_c-pr-.ic1`, size `0x1000`, CRC `0xac4421b1`.
- Current scan of `D:\emu\irem\M72\nspirit.zip` finds `48/48` artifacts present for World `nspirit` and Japan `nspiritj`.
- Current exact-path scan of `D:\emu\irem\M72\nspirit.zip`, `D:\emu\irem\M72\gallopm72.zip`, and `D:\emu\irem\M72\gallop.zip` finds `43/44` artifacts present for `gallopm72` plus World `nspirit`, missing only `gallopm72:mcu:cc_c-pr-.ic1` (`0xac4421b1`). Direct ZIP inspection confirms `D:\emu\irem\M72\nspirit.zip` contains `nin_c-pr-b.ic1` (`0x0f7b2713`) and `nspiritj/nin_c-pr-.ic1` (`0x802d440a`).
- If a lawful Gallop M72 MCU dump becomes available, the scanner points at this unpacked destination: `D:\emu\irem\m72\gallop\cc_c-pr-.ic1`. Equivalent ZIP entries are also valid if the matching set ZIP is rebuilt with the same filename and CRC.
- `nspiritj` is a valid Japan variant and has `nspiritj/nin_c-pr-.ic1`, CRC `0x802d440a`; it is separate from the now-present World `nspirit` MCU `nin_c-pr-b.ic1`, CRC `0x0f7b2713`.
- `scripts\irem_m72\run-corpus-smoke.ps1 -RomDir D:\emu\irem\M72 -Recurse -Set nspirit,nspiritj -Frames 120 -FallbackFrames 300` now passes `2/2`; the runner keeps the exact-stem `nspirit` ZIP route and also discovers the manifest-named `nspiritj/` collection folder inside the same ZIP.
- M82 validation now supports broad `MNEMOS_M82_SET_DIR=D:\emu\irem` again. The M82 manifest/player data gates rank single-inner wrapper ZIPs before direct set ZIPs and unpacked folders, so complete local R-Type II collection wrappers win over incomplete earlier-sorted candidates such as `D:\emu\irem\M72\rtype2`.
- M82 Major Title local wrapper routes are `D:\emu\irem\Major-Title_Arcade_EN.zip` for parent `majtitle` and `D:\emu\irem\Major-Title_Arcade_JA.zip` for clone `majtitlej`. The Japan wrapper is a split program-only route and requires parent fallback to source shared sound, sample, tile, background, sprite, and PROM media from the parent wrapper.
- The broad `scripts\irem_m72\run-corpus-smoke.ps1 -RomDir D:\emu\irem\m72 -Recurse` count from earlier handoff notes is stale for Ninja Spirit. Re-run it before quoting a broad pass count; the focused current proof above shows `nspirit` and `nspiritj` both pass from `D:\emu\irem\M72`.
- Direct `mnemos_player --system irem_m72 --rom D:\emu\irem\M72\airduel.zip` now identifies the collection as `airduelm72` through the canonical M72 suffix and prefix-preferred provider. It still emits media-validation issues for the missing `ad-00/ad-10/ad-20/ad-30`, tile-bank, and sample entries, so leave Air Duel collection ZIPs out of clean roster counts until those artifacts are present or supplied through a validated parent/source route.
- `MNEMOS_M72_SET_DIR` was intentionally unset in the latest full CTest run, so the full M72 roster golden test skipped. Do not set it to the current partial `D:\emu\irem\m72` tree and call that a full-roster proof; use the smoke runner for partial corpus evidence until all authentic roster artifacts are present.

## Latest Validation Evidence

Validation was run from `C:\dev\emu\Mnemos-irem-arcade` under:

```bat
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && <command>'
```

M52 sound-CPU ownership continuation validation on 2026-06-26:

- Removed the M52 command-fed AY/MSM shortcut: main-CPU sound-command writes now update only the latch and IRQ line; the modeled sound Z80 owns AY/MSM port writes.
- Bumped M52 board state and adapter save-target revisions to `6` because the old synthetic MSM sound-ROM cursor is no longer part of the state payload.
- Added focused synthetic coverage proving a latch write leaves AY/MSM state untouched until the sound Z80 executes its port sequence, plus adapter audio coverage using a declared synthetic set with a real `soundcpu` region.
- Focused build passed for `mnemos_manifests_irem_m52_system_test`, `mnemos_apps_player_irem_m52_adapter_test`, and `mnemos_player`.
- Focused CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `4/4` passed for M52 manifest, system, adapter, and local corpus golden tests.
- `clang-format --dry-run --Werror` passed for touched M52 C++ files; `git diff --check` passed with only recurring LF-to-CRLF warnings.
- Full build passed: `cmake --build --preset windows-msvc-debug`.
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84, M90, broad-root M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- Data-gated helper passed: `scripts\run-data-gated-tests.ps1 -BuildDir build\windows-msvc-debug` reported `26/26` selected tests passed plus M72 corpus smoke `2/2`.
- This is M52 sound-ownership progress only; it is not final Moon Patrol sound-port timing, MSM5205 stream timing, discrete analog behavior, graphics parity, or audio parity proof.

Final handoff validation completed on 2026-06-25 21:05 -05:00:

- Full preset build: `cmake --build --preset windows-msvc-debug`
- Full preset CTest: `188/188`, with expected conformance/media skips where ROM or oracle env vars were unset
- Data-gated script with local Irem env vars: `24/24` selected tests, `0` failures
- Irem M72 corpus smoke from the data-gated script: `2/2` passed for configured R-Type/protected/vertical artifacts
- M72 roster golden still skipped because `MNEMOS_M72_SET_DIR` was intentionally unset. This older validation is superseded by the later 2026-06-26 exact ZIP recheck: World/Japan Ninja Spirit are complete in `D:\emu\irem\M72\nspirit.zip`, while Gallop M72 still lacks `cc_c-pr-.ic1`.
- CPS2 corpus smoke skipped because no CPS2 env vars were set for this Irem handoff

M52 Moon Patrol first-pass validation on 2026-06-26:

- `git diff --check`: clean, with only existing LF-to-CRLF working-copy warnings.
- `clang-format --dry-run --Werror` over touched C++ headers/sources passed; `m52_embedded_game_manifests.hpp.in` was intentionally excluded because it contains CMake placeholders.
- Inventory refresh: `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json` reported `123` items, `71` tracked items, `65` directly player-loadable items, `6` tracked metadata-only artifacts, and `2` M52 manifest matches.
- Focused M52/player build passed for `mnemos_manifests_irem_m52_test`, `mnemos_manifests_irem_m52_system_test`, `mnemos_apps_player_irem_m52_adapter_test`, `mnemos_apps_player_adapters_common_test`, `mnemos_apps_player_capability_summary_test`, and `mnemos_player`.
- Focused CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `6/6` passed, including `mnemos_apps_player_irem_m52_corpus_golden_test`.
- Direct player screenshot smoke passed for `D:\emu\irem\Moon-Patrol_Arcade_EN.zip` and `D:\emu\irem\Moon-Patrol_Arcade_EN (1).zip`, both writing `240x252` nonblank PPMs under `build\scratch\irem-m52\`.
- Full serialized build under the Windows MSVC preset completed with `ninja: no work to do` after the prior full link finished.
- Full preset CTest: `201/201`, `0` failures, with expected conformance/media skips where external corpora/env vars were unset.
- Post-doc-edit focused CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `6/6` passed again.
- M52 remains first-pass only: no correct graphics/music claim is made.

M52 dual-SSG audio continuation validation on 2026-06-26:

- Replaced the M52 one-bit audio probe with two native YM2149/AY-compatible SSG chips wired into the board save state, adapter chip list, audio drain path, and capability discovery.
- Focused build passed for `mnemos_manifests_irem_m52_system_test`, `mnemos_apps_player_irem_m52_adapter_test`, `mnemos_apps_player_capability_summary_test`, and `mnemos_player`.
- Focused CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `5/5` passed for M52 manifests, M52 system, capability summary, adapter, and the local Moon Patrol corpus gate.
- Direct player capability smoke against `D:\emu\irem\Moon-Patrol_Arcade_EN.zip` reported `memory.ym2149_0.registers` and `memory.ym2149_1.registers` with `media.rom_set state=available`.
- This is partial AY progress only; the M52 sound CPU, MSM5205, analog path, and audio parity remain open.

M52 MSM5205 continuation validation on 2026-06-26:

- Added `src\chips\audio\msm5205`, a native OKI MSM5205 ADPCM decoder with VCLK-driven nibble consumption, register introspection, audio capture, chip-factory registration, and save/load tests.
- Wired M52 to own one MSM5205, feed deterministic command-seeded nibbles from the loaded `soundcpu` ROM, serialize decoder phase in board state version 3, expose the chip through the adapter, and mix its captured samples with both SSG queues.
- Focused build passed for `mnemos_chips_audio_msm5205_test`, `mnemos_manifests_irem_m52_system_test`, `mnemos_apps_player_irem_m52_adapter_test`, `mnemos_apps_player_capability_summary_test`, and `mnemos_player`.
- Focused CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `6/6` passed, including the local Moon Patrol corpus gate.
- Direct player capability smoke against `D:\emu\irem\Moon-Patrol_Arcade_EN.zip` reported `memory.msm5205.registers`, `memory.ym2149_0.registers`, `memory.ym2149_1.registers`, and `media.rom_set state=available`.
- Full build passed: `cmake --build --preset windows-msvc-debug --parallel 1`.
- Full CTest with clean local Irem gates for M52, M72 R-Type/protected/vertical, M15, M81, M82, M84, and M107 while leaving `MNEMOS_M72_SET_DIR` cleared: `202/202`, with expected conformance/media skips and the expected M72 full-roster skip.
- Still partial: this is not the final Moon Patrol sound CPU protocol, real MSM5205 stream timing, or discrete analog audio path.

M52 sound-CPU ownership continuation validation on 2026-06-26:

- Wired M52 to own and schedule a second Z80 sound CPU with mapped `soundcpu` ROM, `$f000-$ffff` sound RAM, sound-command latch IRQ/ack state, modeled AY/MSM port writes, board save-state version 4, adapter save-target revision 4 at that time, and adapter/capability exposure for the sound Z80.
- Focused synthetic system coverage now proves the sound CPU runs, reads/acks the sound latch, writes modeled AY/MSM ports, persists sound RAM, and restores the sound CPU register state.
- `clang-format --dry-run --Werror` over touched C++ headers/sources passed.
- `git diff --check` passed, with only existing LF-to-CRLF working-copy warnings.
- Focused build passed for `mnemos_manifests_irem_m52_system_test`, `mnemos_apps_player_irem_m52_adapter_test`, `mnemos_apps_player_capability_summary_test`, and `mnemos_player`.
- Focused CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `5/5` passed, including the local Moon Patrol corpus gate.
- Direct player capability smoke against `D:\emu\irem\Moon-Patrol_Arcade_EN.zip` reported `memory.z80_0.registers`, `memory.z80_1.registers`, both YM2149 register surfaces, `memory.msm5205.registers`, and `media.rom_set state=available`.
- Full serialized build passed: `cmake --build --preset windows-msvc-debug --parallel 1`.
- Full CTest with clean local Irem gates for M52, M72 R-Type/protected/vertical, M15, M81, M82, M84, and M107 while leaving `MNEMOS_M72_SET_DIR` cleared: `202/202`, with expected conformance/media skips and the expected M72 full-roster skip.
- Still partial: M52 now has sound CPU ownership and scheduling, but the exact Moon Patrol sound CPU port map, MSM5205 stream timing, and discrete analog path remain unproven.

M52 service/test input continuation validation on 2026-06-26:

- Mapped explicit frontend `test` input to the M52 operator-test system bit `0x10`, while `service` / legacy `mode` continue to drive the service-credit bit `0x08`.
- Adapter save/load coverage now asserts an input snapshot with start, coin, service, and operator-test asserted restores the exact board-visible system byte.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_apps_player_irem_m52_adapter_test mnemos_player`
- Focused M52 CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m52_test`
  - `mnemos_manifests_irem_m52_system_test`
  - `mnemos_apps_player_irem_m52_adapter_test`
  - `mnemos_apps_player_irem_m52_corpus_golden_test`
- Full preset build:
  - `cmake --build --preset windows-msvc-debug`
- Full preset CTest without ROM env vars in that command environment: `206/206`, with expected media/conformance skips.
- `clang-format --dry-run --Werror` passed for the touched M52 C++ file.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proves the M52 player-to-board service/test input path and persistence. It does not prove runtime DIP behavior beyond current manual defaults, Moon Patrol video priority, raster timing, or final visual/audio parity.

M52 manual DIP metadata continuation validation on 2026-06-26:

- Added 13 manual-backed Moon Patrol SW1/SW2 DIP entries to the `mpatrol` manifest: patrol cars, additional car, conditional Coin Mode 1/2 pricing, flip picture, cabinet type, coin mode, unused switch 4, freeze screen, sector selection, demo mode, and test mode.
- The M52 adapter now retains parsed DIP metadata, folds the manual's active-high defaults into board-visible `dsw1=0x01` / `dsw2=0x02`, exposes `DIP switches=13`, and still honors explicit `--dip` override.
- M52 board save-state version and adapter save-target revision are now `5` because the corrected DIP identity intentionally rejects older placeholder `0xff/0xff` snapshots.
- Source evidence: Moon Patrol Instruction Manual game-adjustment and diagnostic DIP switch tables; the diagnostic text says `1` is ON and `0` is OFF.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m52_test mnemos_manifests_irem_m52_system_test mnemos_apps_player_irem_m52_adapter_test mnemos_player`
- Focused M52 CTest with `MNEMOS_M52_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m52_test`
  - `mnemos_manifests_irem_m52_system_test`
  - `mnemos_apps_player_irem_m52_adapter_test`
  - `mnemos_apps_player_irem_m52_corpus_golden_test`
- Full preset build:
  - `cmake --build --preset windows-msvc-debug`
- Full preset CTest without ROM env vars in that command environment: `206/206`, with expected media/conformance skips.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proves manual-backed M52 DIP metadata/default plumbing. It does not prove runtime DIP UI parity, Moon Patrol video priority, raster timing, sound timing, or final visual/audio parity.

M107 V33/V35 model-clock continuation validation on 2026-06-26:

- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_chips_cpu_v30_test mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test`
- Focused CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `5/5` passed for the V-series core metadata test, M107 board tests, M107 adapter tests, and real M107 corpus gate; V30 singlestep corpus skipped as expected because `MNEMOS_V30_TESTS_DIR` was unset.
- Direct capability smoke after rebuilding `mnemos_player`: `mnemos_player --system irem_m107 --rom D:\emu\irem\M107\airass.zip --capabilities` now reports `debug.v33.cpu_trace`, `debug.v35.cpu_trace`, `memory.v33.registers`, `memory.v35.registers`, `audio.ga20.samples`, and `media.rom_set state=available`.
- `git diff --check`: clean, with only existing LF-to-CRLF working-copy warnings.
- Full build: `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, broad-root M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `189/189`, with expected conformance/media skips and the expected M72 roster skip.

M107 save-state identity continuation validation on 2026-06-26:

- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test`
- Focused CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3` passed for M107 board, adapter, and real corpus gates.
- M107 board-state revision is now `4`; tests assert the written revision and reject a version-3 board chunk.
- `git diff --check`: clean, with only existing LF-to-CRLF working-copy warnings.
- Full build: `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, broad-root M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `189/189`, with expected conformance/media skips and the expected M72 roster skip.

M72 exact-path artifact recheck on 2026-06-26:

- `scripts\irem_m72\find-missing-artifacts.ps1 -Root 'D:\emu\irem\M72\nspirit.zip','D:\emu\irem\M72\gallopm72.zip','D:\emu\irem\M72\gallop.zip' -Set 'gallopm72,nspirit'`: `43/44` present and missing only `gallopm72:mcu:cc_c-pr-.ic1` (`0xac4421b1`). The same current corpus has `nspirit:mcu:nin_c-pr-b.ic1` (`0x0f7b2713`) inside `D:\emu\irem\M72\nspirit.zip`.
- `scripts\irem_m72\find-missing-artifacts.ps1 -Root 'D:\emu\irem\M72\nspirit.zip' -Set nspirit,nspiritj`: `48/48` present for the World and Japan Ninja Spirit routes.
- `scripts\irem_m72\run-corpus-smoke.ps1 -RomDir D:\emu\irem\M72 -Recurse -Set nspirit,nspiritj -Frames 120 -FallbackFrames 300`: `2/2` passed after the runner learned to keep exact-stem ZIP candidates while also discovering manifest-named collection folders.
- Post-patch validation: `git diff --check` clean except the existing LF-to-CRLF working-copy warnings; `cmake --build --preset windows-msvc-debug` reported `ninja: no work to do`; `ctest --preset windows-msvc-debug --output-on-failure` passed `210/210` with expected ROM/oracle-gated skips.

M107 GA20 player-mixer continuation validation on 2026-06-26:

- Exact M72 archive scan superseded by the later 2026-06-26 recheck above: `nspirit` is now complete in `D:\emu\irem\M72\nspirit.zip`; `gallopm72:mcu:cc_c-pr-.ic1` remains missing.
- Direct ZIP inspection now shows `D:\emu\irem\M72\nspirit.zip` contains both `nin_c-pr-b.ic1` for World `nspirit` and `nspiritj/nin_c-pr-.ic1` for Japan `nspiritj`; `D:\emu\irem\M72\gallopm72.zip` and `D:\emu\irem\M72\gallop.zip` still do not contain `cc_c-pr-.ic1`.
- Focused build: `cmake --build --preset windows-msvc-debug --target mnemos_chips_audio_irem_ga20_test mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test`
- Focused CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `4/4` passed for `mnemos_chips_audio_irem_ga20_test`, `mnemos_manifests_irem_m107_test`, `mnemos_apps_player_irem_m107_adapter_test`, and `mnemos_apps_player_irem_m107_corpus_golden_test`
- `git diff --check`: clean, with only existing LF-to-CRLF working-copy warnings.
- Full build: `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M81, broad-root M82, M84, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `189/189`, with expected conformance/media skips and the expected M72 roster skip.

Continuation validation on 2026-06-25 21:19 -05:00:

- `scripts\irem_m72\find-missing-artifacts.ps1 -Root D:\emu\irem -Recurse -Set gallopm72,nspirit`: now `43/44` present in the current corpus, missing only `cc_c-pr-.ic1`, with suggested placement under `D:\emu\irem\m72\gallop\`
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
- Current `D:\emu\irem\M72\nspirit.zip` artifact scan: `48/48` present for `nspirit` and `nspiritj`; World `nspirit` now has `nin_c-pr-b.ic1` (`0x0f7b2713`) and Japan `nspiritj` has `nspiritj/nin_c-pr-.ic1` (`0x802d440a`).
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

M84 Lightning Swords / V35 continuation validation on 2026-06-26:

- Added `ltswords` as a checked-in M84 manifest for the local unpacked folder at `D:\emu\irem\M72\ltswords`, with V35 board-parameter selection and explicit `irem_m84_prom_pld` HLE metadata for the missing small PROM/PLD artifacts.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json`: `123` items, `72` manifest matches, `66` direct player-loadable routes; `D:\emu\irem\M72\ltswords` is tracked as M84 and direct unpacked-folder loadable.
- Configure/build:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug`
- Focused M84 CTest with `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72`: `3/3`
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_irem_m84_adapter_test`
  - `mnemos_apps_player_irem_m84_corpus_golden_test`
- Direct player smoke:
  - `mnemos_player --system irem_m84 --rom D:\emu\irem\M72\ltswords --save-state build\scratch\irem_m84_ltswords.mns --frames 120`
  - `mnemos_player --system irem_m84 --rom D:\emu\irem\M72\ltswords --load-state build\scratch\irem_m84_ltswords.mns --screenshot build\scratch\irem_m84_ltswords.ppm --frames 1`
  - Screenshot proof: `384x256`, `294704` nonzero pixel bytes, `256` unique byte values.
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M81, broad-root M82, M84 including `ltswords`, M90, M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `202/202`, with the expected M72 roster and non-Irem media/conformance skips.

M84 Gallop / Cosmic Cop continuation validation on 2026-06-26:

- Added `gallop` as a checked-in M84 manifest for the complete local archive at `D:\emu\irem\M72\gallop.zip`, with V35 board-parameter selection and CRC-verified program, sound, graphics, sample, PROM, and PLD regions.
- The local `D:\emu\irem\M72` bucket also contains an unpacked `gallop` folder with different true-M72-style filenames. The M84 manifest and adapter corpus indexers now prefer an exact `<set>.zip` over a same-name folder when both are present, so the complete M84 ZIP is selected for Gallop proof.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json`: `123` items, `74` tracked items, `68` direct player-loadable routes by route shape, and `5` M84 manifest-matching items across four checked-in M84 sets.
- Focused build targets:
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_irem_m84_adapter_test`
  - `mnemos_player`
- Focused M84 CTest with `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72`: `3/3`
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_irem_m84_adapter_test`
  - `mnemos_apps_player_irem_m84_corpus_golden_test`
- Direct player smoke:
  - `mnemos_player --system irem_m84 --rom D:\emu\irem\M72\gallop.zip --save-state build\scratch\irem_m84_gallop.mns --frames 120`
  - `mnemos_player --system irem_m84 --rom D:\emu\irem\M72\gallop.zip --load-state build\scratch\irem_m84_gallop.mns --screenshot build\scratch\irem_m84_gallop.ppm --frames 1`
  - Screenshot proof: `384x256`, `98304` pixels, `247` unique RGB colors, `98304` nonblack pixels.
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M81, broad-root M82, M84 including `gallop`, M90, M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `202/202`, with expected conformance/media skips and the expected M72 roster skip.

M84 Gallop DIP-default continuation validation on 2026-06-26:

- Added 10 Gallop DIP switch definitions to the M84 `gallop` manifest and verified the composed raw default is `0xf9bf`.
- The M84 adapter now retains parsed DIP metadata, applies manifest defaults before board assembly, and exposes a `DIP switches = 10` player spec field for Gallop.
- Focused build targets:
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_irem_m84_adapter_test`
  - `mnemos_player`
- Focused M84 CTest with `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72`: `3/3`
  - `mnemos_manifests_irem_m84_test`
  - `mnemos_apps_player_irem_m84_adapter_test`
  - `mnemos_apps_player_irem_m84_corpus_golden_test`
- Direct player smoke:
  - `mnemos_player --system irem_m84 --rom D:\emu\irem\M72\gallop.zip --save-state build\scratch\irem_m84_gallop_dip.mns --frames 120`
  - `mnemos_player --system irem_m84 --rom D:\emu\irem\M72\gallop.zip --load-state build\scratch\irem_m84_gallop_dip.mns --screenshot build\scratch\irem_m84_gallop_dip.ppm --frames 1`
  - Screenshot proof: `384x256`, `98304` pixels, `247` unique RGB colors, `98304` nonblack pixels.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M81, broad-root M82, M84 including `gallop`, M90, M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `202/202`, with expected conformance/media skips and the expected M72 roster skip.

M75 Vigilante first-pass continuation validation on 2026-06-26:

- Added checked-in M75 Vigilante manifest, executable Z80/Z80/YM2151/DAC board, player adapter, CLI family routing, inventory grouping, and docs inventory updates.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json`: `123` items, `75` manifest matches, `69` direct player-loadable routes, `6` metadata-only tracked routes, and `1` M75 manifest match for `vigilant`.
- Configure/build:
  - `cmake --preset windows-msvc-debug`
  - focused build target set included `mnemos_manifests_irem_m75_test`, `mnemos_manifests_irem_m75_system_test`, `mnemos_apps_player_irem_m75_adapter_test`, `mnemos_apps_player_capability_summary_test`, `mnemos_apps_player_adapters_common_test`, and `mnemos_player`
- Focused M75/adapters CTest with `MNEMOS_M75_SET_DIR=D:\emu\irem`: `6/6`
  - `mnemos_manifests_irem_m75_test`
  - `mnemos_manifests_irem_m75_system_test`
  - `mnemos_apps_player_capability_summary_test`
  - `mnemos_apps_player_adapters_common_test`
  - `mnemos_apps_player_irem_m75_adapter_test`
  - `mnemos_apps_player_irem_m75_corpus_golden_test`
- Direct player smoke:
  - `mnemos_player --system irem_m75 --rom "D:\emu\irem\Vigilante_Arcade_EN (3).zip" --save-state build\scratch\irem_m75_vigilant.mns --frames 120`
  - `mnemos_player --system irem_m75 --rom "D:\emu\irem\Vigilante_Arcade_EN (3).zip" --screenshot build\scratch\irem_m75_vigilant.ppm --frames 120`
  - Screenshot proof: `256x256`, `196608` payload bytes, `195761` nonzero payload bytes, SHA-256 `192d3ead0f72dfc049bfe6490fc909c2ea087bdab146159aa962e3e59e7dd39f`
  - Save-state proof: `build\scratch\irem_m75_vigilant.mns`, `122423` bytes after 120 frames
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.

M75 palette/rear-color continuation validation on 2026-06-26:

- M75 palette RAM now routes through a two-bank 5-bit KNA91-style CPU-visible contract instead of raw RAM mapping: CPU writes retain only bits 0-4, reads return the stored 5-bit value with high bits set, and bank `0x400` is used by the rear/background palette path.
- The Vigilante rear-color output now preserves bit 6 as the rear-layer disable flag and masks the color code through bits `0,2,3` for the diagnostic rear-background palette selection.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m75_system_test`
- Focused CTest:
  - `ctest --preset windows-msvc-debug --output-on-failure -R mnemos_manifests_irem_m75_system_test`: `1/1`
- Focused M75 CTest with `MNEMOS_M75_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m75_test`
  - `mnemos_manifests_irem_m75_system_test`
  - `mnemos_apps_player_irem_m75_adapter_test`
  - `mnemos_apps_player_irem_m75_corpus_golden_test`
- Direct player smoke:
  - `mnemos_player --system irem_m75 --rom "D:\emu\irem\Vigilante_Arcade_EN (3).zip" --save-state build\scratch\irem_m75_vigilant_palette.mns --frames 120`
  - `mnemos_player --system irem_m75 --rom "D:\emu\irem\Vigilante_Arcade_EN (3).zip" --screenshot build\scratch\irem_m75_vigilant_palette.ppm --frames 120`
  - Screenshot proof: `256x256`, `196608` payload bytes, `195761` nonzero payload bytes, SHA-256 `192d3ead0f72dfc049bfe6490fc909c2ea087bdab146159aa962e3e59e7dd39f`
  - Save-state proof: `build\scratch\irem_m75_vigilant_palette.mns`, `122423` bytes after 120 frames
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.

M75 official clone parent-fallback validation on 2026-06-26:

- Added checked-in M75 manifests for official Vigilante clones `vigilanta`, `vigilantb`, `vigilantc`, `vigilantd`, `vigilantg`, and `vigilanto`, each declaring parent `vigilant` and only the changed program/graphics regions needed by the local split wrappers.
- Added M75 adapter parent fallback resolution for `parent.zip`, unpacked parent directories, and sibling single-inner ZIP wrappers such as `D:\emu\irem\Vigilante_Arcade_EN (3).zip`.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json`: `123` items, `81` manifest matches, `75` direct player-loadable routes, `6` metadata-only tracked routes, and `7` M75 manifest matches for the parent plus official clones.
- Focused configure/build/test:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m75_test mnemos_manifests_irem_m75_system_test mnemos_apps_player_irem_m75_adapter_test mnemos_player`
  - `MNEMOS_M75_SET_DIR=D:\emu\irem ctest --preset windows-msvc-debug --output-on-failure -R "irem_m75"`: `4/4`
- Direct Japan-clone smoke:
  - `mnemos_player --system irem_m75 --rom "D:\emu\irem\Vigilante_Arcade_JA.zip" --save-state build\scratch\irem_m75_vigilantd_clone.mns --frames 120`
  - `mnemos_player --system irem_m75 --rom "D:\emu\irem\Vigilante_Arcade_JA.zip" --screenshot build\scratch\irem_m75_vigilantd_clone.ppm --frames 120`
  - Screenshot proof: `256x256`, `196608` payload bytes, `195738` nonzero payload bytes, SHA-256 `7968e643bef602092d3082bcf3fd5ddf4665807ea673aa8ac5bbac8005195c30`
  - Save-state proof: `build\scratch\irem_m75_vigilantd_clone.mns`, `122490` bytes after 120 frames
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.

M75 sample ROM/DAC continuation validation on 2026-06-26:

- Added `m75 sound CPU streams sample ROM bytes through the DAC`, a synthetic Z80 proof that the M75 sound CPU writes the low/high sample-address ports, reads consecutive bytes from the sample ROM through port `$84`, stores them in sound RAM, writes them through the DAC port, and acknowledges the sound latch.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m75_system_test`
- Focused M75 system CTest:
  - `ctest --preset windows-msvc-debug -R mnemos_manifests_irem_m75_system_test --output-on-failure`: `1/1`
- Focused M75 CTest with `MNEMOS_M75_SET_DIR=D:\emu\irem`: `4/4`
- `clang-format --dry-run --Werror` passed for the touched M75 C++ file.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full preset build:
  - `cmake --build --preset windows-msvc-debug`
- Full preset CTest without ROM env vars in that command environment: `206/206`, with expected media/conformance skips.
- This proves the currently modeled sample-address/sample-read/DAC plumbing. It does not prove board-reference sample timing, analog filtering/mixing, or final Vigilante audio parity.

M75 service/test input continuation validation on 2026-06-26:

- Mapped explicit frontend `test` input to the M75 operator-test system bit `0x20`, while `service` / legacy `mode` continue to drive the service-credit bit `0x10`.
- Adapter save/load coverage now asserts an input snapshot with start, coin, service, and operator-test asserted restores the exact board-visible system byte.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_apps_player_irem_m75_adapter_test mnemos_player`
- Focused M75 CTest with `MNEMOS_M75_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m75_test`
  - `mnemos_manifests_irem_m75_system_test`
  - `mnemos_apps_player_irem_m75_adapter_test`
  - `mnemos_apps_player_irem_m75_corpus_golden_test`
- Full preset build:
  - `cmake --build --preset windows-msvc-debug`
- Full preset CTest without ROM env vars in that command environment: `206/206`, with expected media/conformance skips.
- `clang-format --dry-run --Werror` passed for the touched M75 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proves the M75 player-to-board service/test input path and persistence. It does not prove runtime DIP behavior beyond current manual defaults, Vigilante video priority, raster timing, or final visual/audio parity.

M75 manual DIP metadata continuation validation on 2026-06-26:

- Added 14 manual-backed Vigilante SW1/SW2 DIP entries to the `vigilant` manifest: fighters, difficulty, energy decrease, conditional Coin Mode 1/2 coinage, flip picture, cabinet type, coin mode, demo sound, buy-in, demo freeze, no-death, and switch 8.
- The M75 adapter now retains parsed DIP metadata, folds manifest defaults into board-visible active-low `dsw1=0xff` / `dsw2=0xfd`, exposes `DIP switches=14`, and still honors explicit `--dip` override.
- Source evidence: Vigilante Installation & Service Manual game-options tables for DIP SWITCH 1 and DIP SWITCH 2.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m75_test mnemos_apps_player_irem_m75_adapter_test mnemos_player`
- Focused M75 CTest with `MNEMOS_M75_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m75_test`
  - `mnemos_manifests_irem_m75_system_test`
  - `mnemos_apps_player_irem_m75_adapter_test`
  - `mnemos_apps_player_irem_m75_corpus_golden_test`
- `clang-format --dry-run --Werror` passed for the touched M75 C++ files.
- Full preset build:
  - `cmake --build --preset windows-msvc-debug`
- Full preset CTest without ROM env vars in that command environment: `206/206`, with expected media/conformance skips.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proves manual-backed M75 DIP metadata/default plumbing. It does not prove runtime DIP UI parity, Vigilante video priority, raster timing, sound timing, or final visual/audio parity.

M92 GunForce clone parent-fallback validation on 2026-06-26:

- Added checked-in M92 manifests for `gunforcej` and `gunforceu`, each declaring parent `gunforce` and only the changed main CPU ROMs from the local split wrappers.
- Added M92 adapter parent fallback resolution for direct parent ZIPs, unpacked parent directories, and sibling single-inner wrapper ZIPs.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-corpus\inventory-m92-clones.json`: `123` items, `83` tracked items, `77` direct player-loadable routes, `6` metadata-only tracked routes, and `7` M92 manifest matches. The M72-bucket board-family candidates dropped from 10 to 8 because `gunforcej` and `gunforceu` are now tracked M92 sets.
- Configure/build:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m92_test mnemos_manifests_irem_m92_system_test mnemos_apps_player_irem_m92_adapter_test`
- Focused M92 CTest with `MNEMOS_M92_SET_DIR=D:\emu\irem\M72`: `4/4`
  - `mnemos_manifests_irem_m92_test`
  - `mnemos_manifests_irem_m92_system_test`
  - `mnemos_apps_player_irem_m92_adapter_test`
  - `mnemos_apps_player_irem_m92_corpus_golden_test`
- Direct player smokes:
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_EN (1).zip" --screenshot build\scratch\irem-m92\gunforceu.ppm --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_EN (1).zip" --save-state build\scratch\irem-m92\gunforceu.mns --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_JA.zip" --screenshot build\scratch\irem-m92\gunforcej.ppm --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\M72\GunForce-Battle-Fire-Engulfed-Terror-Island_Arcade_JA.zip" --save-state build\scratch\irem-m92\gunforcej.mns --frames 1`
  - Screenshot proof: both PPMs are `320x240`, `230400` payload bytes, and nonzero RGB payloads.
  - Save-state proof: both M92 clone save states are `266` bytes after one frame.

M92 Mystic Riders manifest/corpus continuation validation on 2026-06-26:

- Added checked-in M92 manifests for `mysticri`, `gunhohki`, and `mysticrib`. `mysticri` is the complete local parent wrapper route; `gunhohki` and `mysticrib` declare parent `mysticri` and compose through the existing M92 sibling-wrapper parent fallback.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-corpus\inventory-m92-mystic.json`: `123` items, `86` tracked items, `80` direct player-loadable routes, `6` metadata-only tracked routes, and `10` M92 manifest matches. The three Mystic Riders root wrappers now resolve as tracked M92 sets.
- Configure/build:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m92_test mnemos_manifests_irem_m92_system_test mnemos_apps_player_irem_m92_adapter_test mnemos_player`
- Focused M92 CTest with `MNEMOS_M92_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m92_test`
  - `mnemos_manifests_irem_m92_system_test`
  - `mnemos_apps_player_irem_m92_adapter_test`
  - `mnemos_apps_player_irem_m92_corpus_golden_test`
- Direct player smokes:
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Mystic-Riders_Arcade_EN.zip" --screenshot build\scratch\irem-m92\mysticri.ppm --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Mystic-Riders_Arcade_EN.zip" --save-state build\scratch\irem-m92\mysticri.mns --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Mystic-Riders_Arcade_JA.zip" --screenshot build\scratch\irem-m92\gunhohki.ppm --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Mystic-Riders_Arcade_JA.zip" --save-state build\scratch\irem-m92\gunhohki.mns --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Mystic-Riders_Arcade_EN (1).zip" --screenshot build\scratch\irem-m92\mysticrib.ppm --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Mystic-Riders_Arcade_EN (1).zip" --save-state build\scratch\irem-m92\mysticrib.mns --frames 1`
  - Screenshot proof: all three PPMs are `320x240`, `230400` payload bytes, `256` unique payload byte values, and nonzero RGB payloads.
  - Save-state proof: `mysticri.mns` is `147118` bytes, `gunhohki.mns` is `147108` bytes, and `mysticrib.mns` is `147105` bytes after one frame.
- `clang-format --dry-run --Werror` passed for the touched M92 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92 including Mystic Riders, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.

M92 Ninja Baseball Bat Man manifest/corpus continuation validation on 2026-06-26:

- Added checked-in M92 manifests for `nbbatman` and `nbbatmanu`. `nbbatman` is the complete local parent wrapper route; `nbbatmanu` declares parent `nbbatman`, carries the changed lower main-program pair, and declares the unchanged upper main-program pair so clone-parent fallback can source those bytes from the parent wrapper.
- Added the `m92_b_d` first-pass M92 layout identity for the Ninja Baseball Bat Man PLD/layout variant.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-corpus\inventory-m92-nbbatman.json`: `123` items, `88` tracked items, `82` direct player-loadable routes, `6` metadata-only tracked routes, and `12` M92 manifest matches. The two Ninja Baseball root wrappers now resolve as tracked M92 sets.
- Configure/build:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m92_test mnemos_manifests_irem_m92_system_test mnemos_apps_player_irem_m92_adapter_test mnemos_player`
- Focused M92 CTest with `MNEMOS_M92_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m92_test`
  - `mnemos_manifests_irem_m92_system_test`
  - `mnemos_apps_player_irem_m92_adapter_test`
  - `mnemos_apps_player_irem_m92_corpus_golden_test`
- Direct player smokes:
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Ninja-Baseball-Bat-Man_Arcade_EN.zip" --screenshot build\scratch\irem-m92\nbbatman.ppm --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Ninja-Baseball-Bat-Man_Arcade_EN.zip" --save-state build\scratch\irem-m92\nbbatman.mns --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Ninja-Baseball-Bat-Man_Arcade_EN (1).zip" --screenshot build\scratch\irem-m92\nbbatmanu.ppm --frames 1`
  - `mnemos_player --system irem_m92 --rom "D:\emu\irem\Ninja-Baseball-Bat-Man_Arcade_EN (1).zip" --save-state build\scratch\irem-m92\nbbatmanu.mns --frames 1`
  - Screenshot proof: both PPMs are `320x240`, `230400` payload bytes, at least `17` unique sampled payload byte values, and nonzero RGB payloads.
  - Save-state proof: `nbbatman.mns` is `146042` bytes and `nbbatmanu.mns` is `146043` bytes after one frame.
- `clang-format --dry-run --Werror` passed for the touched M92 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92 including Ninja Baseball Bat Man, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.

M82 Major Title manifest/corpus continuation validation on 2026-06-26:

- Added checked-in M82 manifests for `majtitle` and `majtitlej`. `majtitle` is the complete local parent wrapper route; `majtitlej` declares parent `majtitle` and carries the changed main-program ROMs while inheriting shared sound, sample, tile, background, sprite, and PROM media.
- Re-ran `scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-corpus\inventory-m82-majtitle.json`: `123` items, `90` tracked items, `84` direct player-loadable routes, `6` metadata-only tracked routes, and `7` M82 manifest matches.
- Configure/build:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m82_test mnemos_apps_player_irem_m82_adapter_test mnemos_player`
- Focused M82 CTest with `MNEMOS_M82_SET_DIR=D:\emu\irem`: `3/3`
  - `mnemos_manifests_irem_m82_test`
  - `mnemos_apps_player_irem_m82_adapter_test`
  - `mnemos_apps_player_irem_m82_corpus_golden_test`
- Direct player smokes:
  - `mnemos_player --system irem_m82 --rom "D:\emu\irem\Major-Title_Arcade_EN.zip" --screenshot build\scratch\irem-m82\majtitle.ppm --frames 1`
  - `mnemos_player --system irem_m82 --rom "D:\emu\irem\Major-Title_Arcade_EN.zip" --save-state build\scratch\irem-m82\majtitle.mns --frames 1`
  - `mnemos_player --system irem_m82 --rom "D:\emu\irem\Major-Title_Arcade_JA.zip" --screenshot build\scratch\irem-m82\majtitlej.ppm --frames 1`
  - `mnemos_player --system irem_m82 --rom "D:\emu\irem\Major-Title_Arcade_JA.zip" --save-state build\scratch\irem-m82\majtitlej.mns --frames 1`
  - Screenshot proof: both PPMs are `384x256`, `294927` bytes, at least `33` unique sampled payload byte values, and `913` nonzero sampled payload bytes.
  - Save-state proof: both save states are `27133` bytes after one frame.
- `clang-format --dry-run --Werror` passed for the touched M82 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82 including Major Title, M84 including `gallop`, M90, broad-root M92 including Ninja Baseball Bat Man, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.

M82 Major Title background graphics continuation validation on 2026-06-26:

- Wired the checked-in M82 `backgrounds` ROM region through board pinning, raw fallback loading, scanline composition, and the convenience frame compositor. The video path now feeds dedicated background graphics into the rear tilemap when present and preserves the existing `tiles` fallback for R-Type II-style sets.
- Added `m82 video renders a dedicated background graphics region`, which renders the rear tilemap from `backgrounds` while passing an empty foreground tile span. This fails if the background region is ignored or if rear tile rendering still depends on foreground tiles.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m82_test mnemos_apps_player_irem_m82_adapter_test mnemos_player`
- Focused M82 CTest with `MNEMOS_M82_SET_DIR=D:\emu\irem`: `3/3`
  - `mnemos_manifests_irem_m82_test`
  - `mnemos_apps_player_irem_m82_adapter_test`
  - `mnemos_apps_player_irem_m82_corpus_golden_test`
- Direct player smokes:
  - `mnemos_player --system irem_m82 --rom "D:\emu\irem\Major-Title_Arcade_EN.zip" --screenshot build\scratch\irem-m82\majtitle-background.ppm --frames 1`
  - `mnemos_player --system irem_m82 --rom "D:\emu\irem\Major-Title_Arcade_JA.zip" --screenshot build\scratch\irem-m82\majtitlej-background.ppm --frames 1`
- `clang-format --dry-run --Werror` passed for the touched M82 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82 including Major Title, M84 including `gallop`, M90, broad-root M92 including Ninja Baseball Bat Man, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- This proves loader-to-renderer consumption of the Major Title background ROM region and no-crash player execution. It is not final Major Title visual-priority, palette-bank, raster-phase, DIP, or audio parity proof.

M92 sound-command latch continuation validation on 2026-06-26:

- Added explicit M92 sound-command latch/reply helpers. Main V33 sound-command writes now set `sound_latch_pending`; V35 latch reads clear it; V35 reply writes set `sound_reply_pending`; both pending bits are preserved in M92 board save-state version 2.
- Added `m92 sound command latch reaches the V35 and returns a reply`, with one section proving unread commands remain pending and another proving a synthetic V35 sound program reads the command, stores it in sound RAM, and writes it back through the reply port.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m92_system_test mnemos_apps_player_irem_m92_adapter_test mnemos_player`
- Targeted retry after fixing the synthetic program ordering:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m92_system_test && ctest --preset windows-msvc-debug --output-on-failure -R mnemos_manifests_irem_m92_system_test`: `1/1`
- Focused M92 CTest with `MNEMOS_M92_SET_DIR=D:\emu\irem`: `4/4`
  - `mnemos_manifests_irem_m92_test`
  - `mnemos_manifests_irem_m92_system_test`
  - `mnemos_apps_player_irem_m92_adapter_test`
  - `mnemos_apps_player_irem_m92_corpus_golden_test`
- `clang-format --dry-run --Werror` passed for the touched M92 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82 including Major Title, M84 including `gallop`, M90, broad-root M92 including Ninja Baseball Bat Man, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- This proves the modeled command/reply latch contract and save-state persistence. It is not encrypted V35 program decryption, command IRQ timing, GA20 analog behavior, or final M92 audio parity proof.

M107 sound-command latch continuation validation on 2026-06-26:

- Added explicit M107 sound-command latch/reply helpers. Main V33 sound-command writes now set `sound_latch_pending`; V35 latch reads clear it; V35 reply writes set `sound_reply_pending`; both pending bits are preserved in M107 board save-state version 5.
- Added `m107 sound command latch reaches the V35 and returns a reply`, with one section proving unread commands remain pending and another proving a synthetic V35 sound program reads the command, stores it in sound RAM, and writes it back through the reply port.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test mnemos_player`
- Focused M107 CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
  - `mnemos_apps_player_irem_m107_corpus_golden_test`
- `clang-format --dry-run --Werror` passed for the touched M107 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82 including Major Title, M84 including `gallop`, M90, broad-root M92 including Ninja Baseball Bat Man, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- At that point, this proved the modeled M107 command/reply latch contract and save-state persistence. It did not prove command IRQ timing, full V35 on-die peripheral proof, GA20 analog balance/filtering, or final M107 audio parity proof.

M107 memory-mapped sound/main map continuation validation on 2026-06-26:

- Corrected the M107 first-pass map to use VRAM `$d0000`, work RAM `$e0000`, sprite RAM `$f8000-$f8fff`, palette RAM `$f9000-$f9fff`, sound RAM `$a0000-$a3fff`, GA20 `$a8000-$a801f`, YM2151 `$a8040-$a8043`, command latch `$a8044`, and sound reply `$a8046`.
- Removed the false M107 shared RAM window and adapter `shared_ram` memory view; the adapter now exposes only the modeled M107 memory surfaces.
- Bumped M107 board save-state version to `6` and board identity to the map-corrected revision so pre-correction states are rejected.
- Added M107 system assertions for every corrected main/sound window and switched synthetic M107 GA20 plus command/reply programs to the memory-mapped sound path.
- Focused build:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test mnemos_player`
- Focused M107 CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
  - `mnemos_apps_player_irem_m107_corpus_golden_test`
- Direct player smokes:
  - `mnemos_player --system irem_m107 --rom D:\emu\irem\M107\airass.zip --screenshot build\scratch\irem-m107\airass-map.ppm --frames 1`
  - `mnemos_player --system irem_m107 --rom D:\emu\irem\M107\airass.zip --save-state build\scratch\irem-m107\airass-map.mns --frames 1`
  - `mnemos_player --system irem_m107 --rom D:\emu\irem\M107\firebarr.zip --screenshot build\scratch\irem-m107\firebarr-map.ppm --frames 1`
  - `mnemos_player --system irem_m107 --rom D:\emu\irem\M107\firebarr.zip --save-state build\scratch\irem-m107\firebarr-map.mns --frames 1`
  - Screenshot proof: both PPMs are `384x256`, `294912` payload bytes, `256` unique payload byte values, and nonzero RGB payloads (`293877` nonzero bytes for Air Assault, `293764` for Fire Barrel).
  - Save-state proof: `airass-map.mns` is `181041` bytes and `firebarr-map.mns` is `118959` bytes after one frame.
- `clang-format --dry-run --Werror` passed for the touched M107 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82 including Major Title, M84 including `gallop`, M90, broad-root M92 including Ninja Baseball Bat Man, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- At that point, this proved the modeled M107 memory windows and sound-side MMIO route. It did not prove V33/V35 on-die peripheral proof, command IRQ timing proof, GA21/GA22 video parity, DIP/operator I/O proof, or final visual/audio parity.

M107 SW1/SW2 DIP metadata continuation validation on 2026-06-26:

- Added the shared Fire Barrel / Air Assault SW1/SW2 DIP profile to `airass` and `firebarr`: Lives, Allow Continue, Demo Sounds, Service Mode, Flip Screen, Coin Slots, Coin Mode, Coinage, Coin A, and Coin B.
- At that point, the M107 adapter retained manifest DIP metadata, folded the manifest SW1/SW2 defaults into the board DIP word (`0xffbf`), exposed `DIP switches=10` in system spec, and still honored explicit `dip_override`.
- Focused build/configure:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test mnemos_player`
- Focused M107 CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
  - `mnemos_apps_player_irem_m107_corpus_golden_test`
- `clang-format --dry-run --Werror` passed for the touched M107 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- This proved the SW1/SW2 metadata/default path for checked-in M107 sets before the SW3 continuation below. At that point, it did not model the separate SW3 coin/system DIP word, Fire Barrel-specific Rapid Fire / Continuous Play switches, operator-test routing, V33/V35 peripherals, command IRQ timing, GA21/GA22 video, or final parity.

M107 SW3 COINS_DSW3 continuation validation on 2026-06-26:

- Added Fire Barrel / Air Assault SW3 Rapid Fire and Continuous Play DIP metadata to `airass` and `firebarr`.
- The M107 board now owns a separate `coins_dsw3` word for ports `0x02/0x03`, while SW1/SW2 remain on ports `0x04/0x05`; board save-state version is now `7`.
- The M107 adapter folds SW3 defaults into `coins_dsw3` (`0xebff`) separately from SW1/SW2 (`0xffbf`) and exposes `DIP switches=12`.
- A synthetic V33 port-read test now verifies that the CPU sees the dynamic system low byte on port `0x02`, SW3 high byte `0xeb` on port `0x03`, and SW1/SW2 bytes `0xbf/0xff` on ports `0x04/0x05`.
- Focused build/configure:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test mnemos_apps_player_irem_m107_adapter_test mnemos_player`
- Focused M107 CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
  - `mnemos_apps_player_irem_m107_corpus_golden_test`
- `clang-format --dry-run --Werror` passed for the touched M107 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- This proved the SW3 metadata/default and CPU-visible port split for checked-in M107 sets before the operator-service continuation below. At that point, it did not prove operator-test routing, V33/V35 peripherals, command IRQ timing, GA21/GA22 video, or final parity.

M107 operator-service input continuation validation on 2026-06-26:

- Mapped explicit frontend `test` input to the M107 `COINS_DSW3` operator-service bit `0x20`, while `service` / legacy `mode` continue to drive the service-credit bit `0x10`.
- Adapter save/load coverage now asserts an input snapshot with start, coin, service, and operator-test asserted restores the exact board-visible system byte.
- Focused build/configure:
  - `cmake --preset windows-msvc-debug`
  - `cmake --build --preset windows-msvc-debug --target mnemos_apps_player_irem_m107_adapter_test mnemos_player`
- Focused M107 CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
  - `mnemos_apps_player_irem_m107_corpus_golden_test`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- `clang-format --dry-run --Werror` passed for the touched M107 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proved the player-to-board operator-service input path before the command-IRQ continuation below. At that point, it did not prove deeper M107 I/O behavior, V33/V35 peripherals, command IRQ timing, GA21/GA22 video, or final parity.

M107 command IRQ continuation validation on 2026-06-26:

- Replaced the read-clears-command shortcut with the first-pass M107 sound-latch IRQ contract: main V33 sound-command writes assert the sound V35 IRQ line, the V35 INTP1 acknowledge vector is 25, latch reads do not clear pending state, and sound-side writes to `$a8044` acknowledge/clear the command IRQ. YM2151 IRQ is routed through V35 INTP0/vector 24.
- Bumped M107 board save-state version to `8` so pre-command-IRQ states are rejected instead of silently restoring under the new latch/IRQ acknowledge semantics.
- Focused M107 board build/test:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test`
  - `ctest --preset windows-msvc-debug --output-on-failure -R mnemos_manifests_irem_m107_test`: `1/1`
- Focused M107 adapter/corpus build and CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
  - `mnemos_manifests_irem_m107_test`
  - `mnemos_apps_player_irem_m107_adapter_test`
  - `mnemos_apps_player_irem_m107_corpus_golden_test`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- `clang-format --dry-run --Werror` passed for the touched M107 C++ files.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proves the currently modeled V33-to-V35 command IRQ assertion, vector dispatch, explicit acknowledge, and reply path. It does not prove full V35 on-die interrupt-controller priority/latency, GA21/GA22 video, GA20 analog balance/filtering, or final M107 parity.

M107 YM2151 INTP0 IRQ continuation validation on 2026-06-26:

- Added executable M107 board proof that a YM2151 Timer A overflow asserts the board sound IRQ line, uses the V35 INTP0 acknowledge vector `24`, wakes a halted V35, and dispatches a synthetic V35 handler that writes a marker into sound RAM.
- Focused M107 board build/test:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test`
  - `ctest --preset windows-msvc-debug --output-on-failure -R mnemos_manifests_irem_m107_test`: `1/1`
- Focused M107 adapter/corpus CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- `clang-format --dry-run --Werror` passed for the touched M107 C++ file.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proves the currently modeled YM2151-to-V35 INTP0 dispatch path. It does not prove full V35 interrupt-controller priority/latency, GA21/GA22 video, GA20 analog balance/filtering, or final M107 parity.

M107 sound IRQ arbitration continuation validation on 2026-06-26:

- Added synthetic dual-vector V35 proof that when command-latch INTP1 and YM2151 INTP0 are both pending, the modeled M107 IRQ acknowledge chooses YM2151 INTP0/vector 24 first.
- The INTP0 handler writes `0xa0` to sound RAM while the INTP1 handler would write `0xc1`; the test asserts `0xa0` and that the command latch remains pending.
- Focused M107 board build/test:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test`
  - `ctest --preset windows-msvc-debug --output-on-failure -R mnemos_manifests_irem_m107_test`: `1/1`
- Focused M107 adapter/corpus CTest with `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`: `3/3`
- Full build:
  - `cmake --build --preset windows-msvc-debug`
- Full CTest with local Irem env vars set for M72 R-Type/protected/vertical, M15, M52, M75, M81, broad-root M82, M84 including `gallop`, M90, broad-root M92, and M107 while `MNEMOS_M72_SET_DIR` stayed cleared: `206/206`, with expected conformance/media skips and the expected M72 roster skip.
- `clang-format --dry-run --Werror` passed for the touched M107 C++ file.
- `git diff --check` passed with only recurring LF-to-CRLF conversion warnings.
- This proves first-pass modeled arbitration only. It does not prove full V35 interrupt-controller priority/latency, GA21/GA22 video, GA20 analog balance/filtering, or final M107 parity.

M107 sound IRQ follow-on service continuation validation on 2026-06-26:

- Added synthetic V35 proof that when YM2151 INTP0 and command-latch INTP1 begin pending together, the INTP0 handler clears the YM source, re-enables interrupts, and the still-pending command latch is then serviced through INTP1/vector 25.
- Focused M107 board build/test:
  - `cmake --build --preset windows-msvc-debug --target mnemos_manifests_irem_m107_test`
  - `MNEMOS_M107_SET_DIR=D:\emu\irem\M107; ctest --preset windows-msvc-debug -R m107 --output-on-failure`: `3/3`
- Full preset build: `cmake --build --preset windows-msvc-debug` (`ninja: no work to do`)
- Full preset CTest without ROM env vars in that command environment: `206/206`, with expected media/conformance skips.
- This narrows the M107 IRQ gap from priority ordering to cycle-exact V35 interrupt latency and on-die peripheral parity. It does not prove GA21/GA22 video, GA20 analog balance/filtering, or final M107 parity.

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

1. Continue M75 authenticity work: Vigilante tile/sprite/rear-layer priority, exact scroll behavior, reference-backed Z80 sound protocol/sample timing, DIP runtime behavior beyond current manual defaults, raster timing, bootleg coverage, and screenshot/audio parity.
2. Continue M52 authenticity work: Moon Patrol background/road/sprite priority, sound CPU/MSM5205/discrete sound behavior, exact raster timing, runtime DIP behavior beyond current manual defaults, and screenshot/audio parity.
3. Continue M15 authenticity work: board-evidenced discrete sample mappings/analog sound behavior, analog color proof, exact raster phase proof, and screenshot parity.
4. Continue M92 authenticity work: encrypted V35 behavior, GA21/GA22 video/priority, exact M92 memory/I/O, GA20 protocol, DIP/raster behavior, and screenshot/audio parity.
5. Continue M107 authenticity work: V33/V35-specific timing and on-die peripheral behavior, deeper M107 I/O details, full V35 interrupt-controller priority/latency proof, remaining GA20 analog balance/filtering, GA21/GA22 behavior, raster timing, and screenshot parity.
6. Do the M84 authenticity pass and replace or validate the M81-compatible assumptions.
7. Continue M72 artifact closure by locating the exact Gallop M72 MCU dump `cc_c-pr-.ic1` CRC `0xac4421b1`. World Ninja Spirit is now complete in the current local `nspirit.zip`; do not reintroduce the older missing-`nspirit` claim without a fresh CRC scan.
8. Continue authenticity passes for M81/M82/M72 video priority, raster phase/timing, DIP behavior, M81/M82 palette-bank rendering/decode, and board timing.

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
$env:MNEMOS_M52_SET_DIR="D:\emu\irem"
$env:MNEMOS_M75_SET_DIR="D:\emu\irem"
$env:MNEMOS_M81_SET_DIR="D:\emu\irem\M81"
$env:MNEMOS_M82_SET_DIR="D:\emu\irem"
$env:MNEMOS_M84_SET_DIR="D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72"
$env:MNEMOS_M90_SET_DIR="D:\emu\irem"
$env:MNEMOS_M92_SET_DIR="D:\emu\irem"
$env:MNEMOS_M107_SET_DIR="D:\emu\irem\M107"
scripts\run-data-gated-tests.ps1 -BuildDir build\windows-msvc-debug
```
