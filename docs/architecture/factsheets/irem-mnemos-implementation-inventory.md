# Irem Mnemos Implementation Inventory

Generated on 2026-06-26 from the Irem board factsheet and the current
`feature/irem-arcade` worktree.

Primary board taxonomy source:
`docs/architecture/factsheets/irem-system-boards-reference.md`.

Current Mnemos coverage sources:
`src/manifests/irem_*`, `src/apps/player/adapters/irem_*`,
`docs/parity-gap-inventory.md`, and the local metadata command:

```powershell
scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json
```

That scan found 123 local Irem corpus items across the `root`, `M15`, `M72`,
`M81`, `M82`, `M84`, `M107`, and `i8751` buckets. Of those, 71 currently match a
checked-in Mnemos Irem manifest, 65 have a direct player-loadable route through
ZIP, single-inner wrapper ZIP, or unpacked-folder handling, and 6 tracked `.7z`
items remain metadata-only until converted or unpacked. No tracked Irem item is
now contract-only.

## Status Terms

- **Implementation percent** is a conservative board-family completion estimate,
  not a commercial compatibility claim. It combines ROM contracts, CPU/audio/video
  board wiring, save-state support, local corpus proof, and known authenticity
  gaps.
- **Smoke playable** means current Mnemos can load the media, run the player
  route, and has evidence such as nonblank screenshots, save/load, data-gated
  CTest, or corpus smoke. This is weaker than correctness.
- **Correct gfx and music** means visual and audio parity has been proven against
  trusted board/manual/oracle evidence. As of this inventory, no Irem game in
  Mnemos is certified at that level. Several routes are useful and nonblank, but
  screenshot parity, audio parity, DIP behavior, raster phase, priority, and
  protection details remain open.
- **Local corpus bucket names are not board proof.** The current `D:\emu\irem\M72`
  bucket also contains M82, M84, M92, and M107 material from previous sorting.

## Summary Matrix

| Techsheet system | Mnemos profile | Impl. % | Mnemos game/set coverage | Smoke playable now | Correct gfx/music certified | Main remaining work |
|---|---:|---:|---|---|---|---|
| M10 / M15 | `irem_m15` subset | 35% overall / 55% for Head On subset | `headoni` | `headoni` nonblank + save/load | None | M10-family breadth, analog sound/sample mapping, analog color, exact raster phase, screenshot/audio parity |
| M52 | `irem_m52` first-pass | 25% | `mpatrol`, `mpatrolw` | local Moon Patrol wrappers | None | Authentic parallax/road/sprite video, Irem Audio AY/MSM path, Tropical Angel manifests, DIP/raster/audio/video parity |
| M57 | none | 0% | None | None | None | Sparse-board research, manifests, Z80/Irem Audio board path |
| M58 | none | 0% | None | None | None | 10-Yard Fight board classification, manifests, video/sound path |
| M62 | none | 0% | None | None | None | Z80 + M6803 + dual AY/MSM audio stack, KNA customs, large game roster |
| M63 | none | 0% | None | None | None | Sparse-board research, manifests, Z80/Irem Audio board path |
| M72 | `irem_m72` | 70% | 23 checked-in manifests | 19 clean smoke-proven sets | None | Remaining MCU/protection artifacts, no-dump HLE depth, DIP/manual proof, full roster media, visual/audio parity |
| M75 | none | 5% shared M72-family groundwork | None | None | None | Dedicated board identity, Vigilante-era classification, manifests, corpus proof |
| M77 | none | 0% | None | None | None | Board research before implementation |
| M81 | `irem_m81` | 55% | `dbreed`, `hharry`, `xmultipl` | all 3 local sets | None | Video priority, raster timing, DIP proof, palette-bank decode, visual/audio parity |
| M82 | `irem_m82` | 60% | `rtype2`, `rtype2j`, `rtype2jc`, `rtype2m82b` | all 4 local sets | None | Board classification audit, palette-bank decode, raster phase, DIP proof, priority parity, audio parity |
| M84 | `irem_m84` wrapper | 35% | `hharryb`, `hharryu` | both local split sets | None | Replace M81-compatible assumptions, M84 memory/I/O, Hammerin' Harry priority/raster/DIP proof, Cosmic Cop/Ken-Go |
| M85 | none | 5% shared M72-family groundwork | None | None | None | Pound for Pound board identity, manifests, board path |
| M90 / M97 / M99 | `irem_m90` first-pass | 25% | `atompunk`, `newapunk`, `bbmanwj`, `bbmanwja` | all 4 local Atomic Punk/Bomber Man World wrappers | None | Authentic GA25 video, V35 on-die peripherals, complete graphics media, Hasamu/Quiz F-1 manifests, visual/audio parity |
| M92 | `irem_m92` | 32% first-pass | `bmaster`, `gunforce`, `gunforc2`, `hook`, `inthunt` | all 5 data-gated first-pass sets | None | Encrypted V35 sound CPU handling, GA21/GA22 video/priority, exact M92 memory/I/O, protection, DIP/raster/audio/video parity |
| M107 | `irem_m107` | 45% | `airass`, `firebarr` | both data-gated; Air Assault direct nonblank/save-load | None | V33/V35-specific behavior, M107 memory/I/O, GA21/GA22 video, GA20 protocol/analog mix, DIP/raster/parity |
| M119 | none | 0% | None | None | None | Sparse-board research before implementation |

## Correct Graphics And Music Certification

Current certified list: none.

The closest current player routes are useful smoke targets, not final parity
targets:

- M72 has the strongest current game-level route: full V30/Z80/YM2151/video/DAC
  wiring, scanline composition, and 19 clean local smoke sets.
- M52 now has a Moon Patrol ROM-contract/player route for the local parent and
  Williams clone wrappers, but the video and audio are diagnostic first-pass
  paths rather than authentic parallax, road, AY, or MSM5205 behavior.
- M82 has scanline-composed tile/sprite/palette rendering with focused priority
  tests and four R-Type II set routes.
- M15, M81, M84, M92, and M107 all have player-routable first-pass boards with
  nonblank local smoke evidence, but each still has explicit authenticity gaps.

No game should be marked "correct gfx and music" until a later artifact records
visual and audio parity proof.

## Board Detail

### M10 / M15

- **Techsheet games:** IPM Invader, Space Beam, Sky Chuter, Head On, Green Beret,
  Andromeda.
- **Mnemos games:** `headoni`.
- **Smoke playable:** `headoni` loads through `--system irem_m15`, produces a
  224x256 nonblank frame, and has save/load smoke through the local M15 corpus.
- **Correct gfx/music:** not certified. Graphics are first-pass M15
  tile/color/chargen output; sound is board-owned latch/edge evidence, not final
  analog sample or music parity.
- **Remaining:** broader M10/M15 roster, board-evidenced discrete sound and
  analog color, exact raster phase, and screenshot/audio parity.

### M52

- **Techsheet games:** Moon Patrol, Tropical Angel.
- **Mnemos games:** `mpatrol`, `mpatrolw`.
- **Smoke playable:** the local single-inner ZIP wrappers
  `D:\emu\irem\Moon-Patrol_Arcade_EN.zip` and
  `D:\emu\irem\Moon-Patrol_Arcade_EN (1).zip` now match checked-in M52
  manifests. The parent set loads directly; the Williams clone declares
  `mpatrol` as parent and resolves shared sound/PROM/tile/sprite dumps from the
  sibling parent wrapper.
- **Correct gfx/music:** none.
- **Current implementation:** first-pass Z80 board route with the Moon Patrol
  program, sound, text, sprite, and PROM regions, input/DIP MMIO, deterministic
  save-state, adapter capability discovery, and nonblank diagnostic framebuffer
  output. The board now owns two native YM2149/AY-compatible SSG instances:
  command writes program deterministic SSG register state, save/load preserves
  both chips, the adapter mixes both captured stereo queues, and capability
  discovery exposes both register snapshots.
- **Remaining:** replace the diagnostic compositor with board-evidenced
  parallax/road/sprite/text priority, implement the M52 sound CPU/MSM5205 plus
  discrete-analog path beyond the command-driven SSG surface, add Tropical Angel
  coverage if M52/M57 evidence confirms the route, and prove
  DIP/raster/screenshot/audio parity.

### M57

- **Techsheet games:** Tropical Angel.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** research sparse board documentation, then implement manifests,
  Z80 board wiring, video, Irem Audio, inputs/DIPs, and parity proof.

### M58

- **Techsheet games:** 10-Yard Fight.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** classify board-specific differences from M52/M62, then add
  manifests, Z80 board wiring, video/audio, inputs/DIPs, and parity proof.

### M62

- **Techsheet games:** Kung-Fu Master / Spartan X, Kid Niki, Lode Runner,
  Spelunker, Lightning Swords, Youjyuden, The Battle-Road.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** implement the large Z80/M6803 board family, dual AY-3-8910,
  dual MSM5205, KNA custom behavior, title manifests, save-state, and visual/audio
  parity.

### M63

- **Techsheet games:** Wily Tower, Fighting Basketball.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** research sparse board evidence, then add manifests and the
  appropriate late-8-bit board/audio/video path.

### M72

- **Techsheet games:** R-Type, Ninja Spirit, Image Fight, Legend of Hero Tonma,
  Dragon Breed, X-Multiply, Air Duel, Daiku no Gensan.
- **Mnemos checked-in sets:** `airdueljm72`, `airduelm72`, `bchopper`,
  `dbreedjm72`, `dbreedm72`, `dkgensanm72`, `gallopm72`, `imgfight`,
  `imgfightj`, `imgfightjb`, `loht`, `lohtb2`, `lohtb3`, `lohtj`, `mrheli`,
  `nspirit`, `nspiritj`, `rtype`, `rtypeb`, `rtypej`, `rtypejp`, `rtypeu`,
  `xmultiplm72`.
- **Smoke playable / media-clean local proof:** `airdueljm72`, `airduelm72`,
  `bchopper`, `dbreedjm72`, `dbreedm72`, `dkgensanm72`, `imgfight`, `imgfightj`,
  `imgfightjb`, `loht`, `lohtb3`, `mrheli`, `nspiritj`, `rtype`, `rtypeb`,
  `rtypej`, `rtypejp`, `rtypeu`, `xmultiplm72`.
- **Known blocked or incomplete local proof:** `gallopm72` still lacks
  `cc_c-pr-.ic1` CRC `0xac4421b1`; World `nspirit` still lacks
  `nin_c-pr-b.ic1` CRC `0x0f7b2713`; `lohtj` and `lohtb2` still lack complete
  local set-specific artifacts in the broader scans.
- **Current exact local ZIP evidence:** direct CRC scanning of every entry in
  `D:\emu\irem\M72\gallopm72.zip`,
  `D:\emu\irem\M72\gallop.zip`, and
  `D:\emu\irem\M72\nspirit.zip` found no entry with CRC `0xac4421b1` or
  `0x0f7b2713`. The `nspirit.zip` archive contains
  `nspiritj/nin_c-pr-.ic1` with CRC `0x802d440a`, which is the Japan MCU and is
  not a substitute for the World `nin_c-pr-b.ic1` target. An exhaustive CRC scan
  of ZIP members and loose files under `D:\emu\irem\M72` also found zero matches
  for `0xac4421b1` or `0x0f7b2713`.
- **Correct gfx/music:** not certified. The board has the strongest current
  graphics/music implementation, but final visual priority, protection behavior,
  DIP/manual proof, raster phase, and audio parity are still open.
- **Remaining:** finish protected-game behavior with authentic MCU artifacts or
  bounded no-dump HLE, complete the full roster corpus, prove board-manual DIP
  behavior, and add screenshot/audio parity gates.

### M75

- **Techsheet games:** Vigilante is associated with this generation, but the
  factsheet flags exact title-to-board mapping as unverified.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** decide whether M75 needs its own profile or folds into M72 with
  explicit board identity, then add manifests and corpus proof.

### M77

- **Techsheet games:** none confidently listed.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** board research before implementation.

### M81

- **Techsheet games:** Hammerin' Harry, Dragon Breed, X-Multiply.
- **Mnemos games:** `dbreed`, `hharry`, `xmultipl`.
- **Smoke playable:** all three local sets load through `--system irem_m81` with
  resident media validation, rollback-ready state, capability discovery, and
  player smoke.
- **Correct gfx/music:** not certified. Current video/audio are first-pass board
  routes, not final parity.
- **Remaining:** verify or replace first-pass video priority, raster timing,
  DIP behavior, palette-bank rendering/decode, board timing, and visual/audio
  parity.

### M82

- **Techsheet games:** Major Title, plus M82 builds of Air Duel and Daiku no
  Gensan.
- **Mnemos games:** `rtype2`, `rtype2j`, `rtype2jc`, `rtype2m82b`.
- **Smoke playable:** all four local R-Type II sets load through `--system
  irem_m82` with clone-parent fallback and nonblank player smoke.
- **Correct gfx/music:** not certified.
- **Classification note:** the factsheet places R-Type II under M84, while the
  current Mnemos implementation routes the local R-Type II sets through
  `irem_m82`. Keep this as an explicit board-evidence audit item rather than
  treating either label as final.
- **Remaining:** prove board classification, palette-bank rendering/decode,
  exact raster phase, DIP behavior, visual-priority parity, and audio parity.

### M84

- **Techsheet games:** R-Type II, Hammerin' Harry / Daiku no Gensan, Cosmic Cop,
  Ken-Go.
- **Mnemos games:** `hharryb`, `hharryu`.
- **Smoke playable:** both local split sets load through `--system irem_m84` when
  composed with the M81 `hharry` parent media.
- **Correct gfx/music:** not certified.
- **Remaining:** replace or verify the M81-compatible wrapper assumptions with
  board evidence, resolve the R-Type II classification mismatch, add Cosmic
  Cop/Ken-Go coverage if in scope, and prove M84-specific memory/I/O, priority,
  raster timing, DIP behavior, and screenshot/audio parity.

### M85

- **Techsheet games:** Pound for Pound.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** create a dedicated board identity or explicit M84-derived
  profile, then add ROM manifests, video/audio/input wiring, and corpus proof.

### M90 / M97 / M99

- **Techsheet games:** Bomber Man / Bomber Man World / Dyna Blaster / Atomic
  Punk, Hasamu, Quiz F-1.
- **Mnemos games:** `atompunk`, `newapunk`, `bbmanwj`, `bbmanwja`.
- **Smoke playable:** all four current local Atomic Punk/Bomber Man World
  wrappers load CRC-clean through `--system irem_m90`/the M90 adapter data gate,
  step one frame, produce a 384x256 nonblank diagnostic frame, and produce
  save-state bytes.
- **Correct gfx/music:** not certified. The board shell now matches the
  V35/Z80/YM2151/DAC topology, but rendering is a GA25 diagnostic compositor and
  audio proof is limited to the Z80/YM/DAC route plus synthetic DAC mixing.
- **Local corpus note:** `Atomic-Punk_Arcade_EN.zip` currently lives under the
  `D:\emu\irem\M72` storage bucket and unwraps to `atompunk.zip`; the three
  `New-Atomic-Punk-Global-Quest_*` root wrappers unwrap to `newapunk`,
  `bbmanwj`, and `bbmanwja`. The available local wrappers do not include a
  complete GA25 graphics ROM set, so they are route/protection/audio evidence,
  not final visual parity evidence.
- **Remaining:** authentic GA25 tile/sprite/row-scroll behavior, V35 on-die
  interrupt/timer behavior, complete Bomber Man World graphics media, Hasamu and
  Quiz F-1 manifests/corpus proof, DIP behavior, and screenshot/audio parity.

### M92

- **Techsheet games:** Gunforce, R-Type Leo, In the Hunt, Undercover Cops,
  Ninja Baseball Bat Man, Blade Master, Mystic Riders, Major Title 2, Hook,
  Superior/Perfect Soldiers, Gunforce 2.
- **Mnemos games:** `bmaster`, `gunforce`, `gunforc2`, `hook`, `inthunt` as
  checked-in manifests under `irem_m92`.
- **Smoke playable:** all five local wrapper ZIPs load through `--system
  irem_m92`/the M92 adapter data gate, step one frame, produce a 320x240
  nonblank diagnostic frame, and produce save-state bytes.
- **Correct gfx/music:** not certified. Current video is a diagnostic
  region/RAM/PLD-driven first-pass compositor, and current sound only proves the
  YM2151/GA20 shell plus synthetic GA20 MMIO.
- **Local corpus note:** five local M92-era title-wrapper ZIPs now resolve to
  embedded set IDs and load CRC-clean through `MNEMOS_M92_SET_DIR`: Blade Master
  (`bmaster`), Gunforce (`gunforce`), Gunforce 2 (`gunforc2`), Hook (`hook`),
  and In the Hunt (`inthunt`). In the current sorted corpus they live under
  `D:\emu\irem\M72`, which is a storage artifact rather than board proof.
- **Remaining:** encrypted V35 sound CPU handling, exact GA20/YM2151 sound
  protocol, GA21/GA22 video/priority behavior, exact memory/I/O maps,
  protection details, DIP/raster behavior, and visual/audio parity proof.

### M107

- **Techsheet games:** Fire Barrel, Dream Soccer '94, World PK Soccer.
- **Mnemos games:** `airass`, `firebarr`.
- **Smoke playable:** both sets are data-gated through `MNEMOS_M107_SET_DIR`;
  Air Assault has direct nonblank screenshot and save/load smoke. Fire Barrel is
  CRC-clean and player-routable, but not parity-certified.
- **Correct gfx/music:** not certified. Current rendering is an M107-local
  diagnostic path; GA20 exists, but the full sound-CPU protocol and analog
  balance/filtering are not proven.
- **Remaining:** V33/V35-specific timing and on-die peripheral proof, exact M107
  memory/I/O map, GA21/GA22 video and priority behavior, GA20 sound protocol and
  analog mix, DIP behavior, raster timing, and screenshot/audio parity.

### M119

- **Techsheet games:** none confidently listed.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** sparse-board research before implementation.

## Immediate Closure Candidates

1. Add visual/audio parity oracles for one already smoke-playable route before
   promoting any game to "correct gfx/music".
2. Advance M52 Moon Patrol from first-pass route to authentic video/audio by
   replacing the diagnostic compositor and audio probe with board-evidenced
   parallax, road, sprite, AY, and MSM5205 behavior.
3. Resolve the M82/M84 R-Type II classification mismatch with board evidence and
   adjust manifests/docs if needed.
4. Continue M72 artifact closure for `gallopm72` and World `nspirit` by finding
   the exact MCU dumps, without substituting Japan `nspiritj` or synthetic bytes.
5. Advance M90 from a diagnostic V35/Z80/YM/DAC shell to authentic GA25 video
   once complete graphics media and board evidence are available.
6. Advance the M92 first-pass profile from diagnostic execution to authenticity
   by resolving encrypted V35 sound-CPU behavior and GA21/GA22 video evidence.
