# Irem Mnemos Implementation Inventory

Generated on 2026-06-27 from the Irem board factsheet and the current
`feature/irem-arcade` worktree.

Primary board taxonomy source:
`docs/architecture/factsheets/irem-system-boards-reference.md`.

Current Mnemos coverage sources:
`src/manifests/irem_*`, `src/apps/player/adapters/irem_*`,
`docs/parity-gap-inventory.md`, and the local metadata command:

```powershell
scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json
```

That scan found 129 local Irem corpus items across the `root`, `M15`, `M72`,
`M81`, `M82`, `M84`, `M107`, and `i8751` buckets. Of those, 118 currently match
a checked-in Mnemos Irem manifest, 109 are readable through the current ZIP,
single-inner wrapper ZIP, or unpacked-folder media routes, and 96 have an
executable player-supported route. The 1 M14 match, 1 M63 match, and 11 M62
matches are intentionally tracked as contract-only manifests until board/player
profiles exist; 9 tracked `.7z` items remain metadata-only until converted or unpacked. Windows
copy-suffixed checked-in set ZIPs such as `loht (1).zip` are canonicalized to
their embedded manifest IDs for player loading, M72 corpus-smoke grouping, and
inventory grouping. A current all-Irem CRC artifact audit of the checked-in
manifests reports `1274/1274` required files present from `D:\emu\irem`, so there are no current
file-level missing-artifact rows for the checked-in Irem manifest set. The
common data-gated runner now includes the M14 and M63 manifest-load proofs plus
G6-ratcheted corpus golden tests for every implemented Irem player family: M15,
M52, M72, M75, M81, M82, M84, M90, M92, and M107.
For the current Windows local corpus layout, `scripts\irem\run-local-corpus.ps1`
wires `D:\emu\irem` into those data-gated tests, including the mixed-root
M90/M92 wrappers. The strict full-M72 roster gate remains opt-in because it is a
data-heavy player proof, and the current M72 artifact preflight plus
`MNEMOS_M72_SET_DIR=D:\emu\irem` roster CTest are clean.

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
| M14 | `irem_m14` ROM contract | 8% contract-only | `ptrmj` | None; CRC-clean media-load contract only | None | Executable 8085 M14 board profile, video/color, paddle/ball/input behavior, discrete/sample sound, save-state/player adapter, visual/audio parity |
| M52 | `irem_m52` first-pass | 42% | `mpatrol`, `mpatrolw` | local Moon Patrol wrappers; service/test input proof; manual-backed DIP defaults; sound-Z80-owned AY/MSM write proof; RAM/GFX-backed sprite pass; text flip-screen position proof; optional visual/audio hash oracle | None | Authentic parallax/road/background priority, exact sound CPU port/protocol timing, discrete analog path, Tropical Angel manifests, DIP runtime/parity behavior, pinned raster/audio/video parity hashes |
| M57 | none | 0% | None | None | None | Sparse-board research, manifests, Z80/Irem Audio board path |
| M58 | none | 0% | None | None | None | 10-Yard Fight board classification, manifests, video/sound path |
| M62 | `irem_m62` raw-media contracts | 10% contract-only | `battroad`, `horizon`, `ldrun`, `ldruna`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, `spelunk2`, `youjyudn` | None; CRC-clean media-load contract only | None | Executable Z80 + M6803 board profile, dual AY/MSM audio stack, KNA custom video, title bus maps, save-state/player adapter, visual/audio parity |
| M63 | `irem_m63` ROM contract | 8% contract-only | `wilytowr` | None; CRC-clean media-load contract only | None | Executable Z80 + 8039/AY/sample board profile, video/color PROM path, Fighting Basketball manifest, save-state/player adapter, visual/audio parity |
| M72 | `irem_m72` | 70% | 23 checked-in manifests | all 23 checked-in sets are media-clean smoke-proven; `dbreedm72` also has nonzero rendered-audio smoke proof | None | Remaining MCU/protection artifacts, no-dump HLE depth, DIP/manual proof, visual/audio parity |
| M75 | `irem_m75` first-pass | 34% | `vigilant`, `vigilanta`, `vigilantb`, `vigilantbl`, `vigilantc`, `vigilantd`, `vigilantg`, `vigilanto` | local Vigilante parent plus official regional and bootleg clone wrappers; service/test input proof; manual-backed DIP defaults; sound-Z80-clocked DAC event proof | None | Authentic Vigilante graphics priority, DIP runtime UI/override parity, raster phase, reference-backed sound timing, audio parity, bootleg PROM/color behavior proof |
| M77 | none | 0% | None | None | None | Board research before implementation |
| M81 | `irem_m81` | 56% | `dbreed`, `hharry`, `xmultipl` | all 3 local sets; sound-Z80-clocked DAC event proof | None | Video priority, raster timing, DIP proof, palette-bank decode, visual/audio parity |
| M82 | `irem_m82` | 68% | `airduel`, `airduelu`, `majtitle`, `majtitlej`, `rtype2`, `rtype2j`, `rtype2jc`, `rtype2m82b` | all 8 checked-in local sets; local Air Duel M82 parent/US clone wrappers; sound-Z80-clocked DAC event proof | None | Board classification audit, Major Title/Air Duel priority/parity proof, palette-bank decode, raster phase, DIP proof, priority parity, audio parity |
| M84 | `irem_m84` wrapper | 44% | `cosmccop`, `gallop`, `hharryb`, `hharryu`, `ltswords` | both local split sets plus local `ltswords` folder, `gallop.zip`, and `cosmccop.zip`; Gallop/Cosmic Cop DIP default `0xf9bf` | None | Replace M81-compatible assumptions, M84 memory/I/O, Hammerin' Harry/Cosmic Cop/Ken-Go priority/raster, board-authentic DIP proof, `ltswords` PROM/PLD artifacts |
| M85 | none | 5% shared M72-family groundwork | None | None | None | Pound for Pound board identity, manifests, board path |
| M90 / M97 / M99 | `irem_m90` first-pass | 28% | `atompunk`, `newapunk`, `bbmanwj`, `bbmanwja` | all 4 local Atomic Punk/Bomber Man World wrappers; service/test input proof; parsed DIP metadata support; sound-Z80-clocked DAC event proof | None | Authentic GA25 video, V35 on-die peripherals, complete graphics media, Hasamu/Quiz F-1 manifests, board-authentic DIP tables/runtime proof, visual/audio parity |
| M92 | `irem_m92` | 45% first-pass | `bmaster`, `crossbld`, `geostorm`, `gunforce`, `gunforcej`, `gunforceu`, `gunforc2`, `gunhohki`, `hook`, `inthunt`, `inthuntu`, `mysticri`, `mysticrib`, `nbbatman`, `nbbatmanu` | all 15 data-gated first-pass sets; Blade Master Japan, Geostorm, In the Hunt US, GunForce, Mystic Riders, and Ninja Baseball Bat Man direct nonblank/save-state smokes; modeled V35 command/YM IRQ priority proof | None | Encrypted V35 sound CPU behavior/decryption, GA21/GA22 video/priority, exact M92 memory/I/O, protection, DIP/raster/audio/video parity |
| M107 | `irem_m107` | 56% | `airass`, `firebarr` | both data-gated; Air Assault direct nonblank/save-load; shared SW1/SW2 DIP default `0xffbf`; SW3 `COINS_DSW3` default `0xebff`; service/test plus command/YM IRQ priority proof | None | V33/V35-specific behavior, deeper M107 I/O proof, GA21/GA22 video, cycle-exact V35 IRQ latency/GA20 analog mix, raster/parity |
| M119 | none | 0% | None | None | None | Sparse-board research before implementation |

## Correct Graphics And Music Certification

Current certified list: none.

The closest current player routes are useful smoke targets, not final parity
targets:

- M72 has the strongest current game-level route: full V30/Z80/YM2151/video/DAC
  wiring, scanline composition, and 23 clean local smoke sets.
- M52 now has a Moon Patrol ROM-contract/player route for the local parent and
  Williams clone wrappers, but video remains a diagnostic first-pass path rather
  than authentic parallax/road behavior. Audio now routes modeled AY/MSM writes
  through the sound Z80 instead of command-fed shortcuts, but discrete analog
  behavior and timing parity are still open. Its manual-backed DIP defaults are now wired, but runtime DIP
  behavior beyond those defaults remains open. The video path no longer uses
  executable program/sound ROM bytes as pixel entropy, and now renders a
  bounded sprite-RAM record pass through the declared `sprite_gfx` region before
  the text layer while mirroring text-layer final positions under flip-screen.
  `GLD-M52-PARITY-HASH` is registered as a skipped-until-pinned
  visual/audio SHA-256 oracle for a reference-captured M52 set.
- M14 has a single `ptrmj` ROM contract with CRC-clean local wrapper proof, but
  no executable board/player route.
- M62 now has eleven raw-media ROM-set contracts with CRC-clean local wrapper-ZIP
  load proof, but no executable board/player route. Treat these as corpus
  grouping and future board-input evidence only.
- M63 now has a single `wilytowr` ROM contract with CRC-clean local wrapper
  proof, but no executable board/player route.
- M82 has scanline-composed tile/sprite/palette rendering with focused priority
  tests, four R-Type II set routes, and Major Title parent/Japan wrapper routes;
  Major Title's dedicated background ROM region now feeds the rear tilemap when
  present.
- M15, M75, M81, M84, M90, M92, and M107 all have player-routable first-pass boards with
  nonblank local smoke evidence, but each still has explicit authenticity gaps.
  M75 currently covers the complete local Vigilante parent wrapper plus official
  regional and bootleg clone wrappers with a Z80/Z80/YM2151/DAC first-pass route,
  service/test input proof, and Vigilante manual-backed DIP defaults.
  M92 currently covers the complete local GunForce parent wrapper plus the local
  US/Japan split clone wrappers, Blade Master parent/Japan clone, Gunforce 2 /
  Geostorm, In the Hunt parent/US clone, the Mystic Riders parent/Japan/bootleg
  wrappers through clone-parent fallback, plus Ninja Baseball Bat Man parent and
  US split-wrapper routes.
  M84 now includes V35-profile `ltswords`, `gallop`, and `cosmccop` routes;
  `ltswords` declares missing PROM/PLD artifacts, while `gallop.zip` supplies
  the complete listed PROM/PLD set and `cosmccop.zip` inherits those artifacts
  through the Gallop parent, but both still need board-parity evidence.

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

### M14

- **Techsheet games:** P.T. Reach Mahjong.
- **Mnemos games:** contract-only ROM manifest for `ptrmj`.
- **Smoke playable:** none. `MNEMOS_M14_SET_DIR=D:\emu\irem` data-gates
  CRC-clean loading for the local single-inner wrapper ZIP, but there is no
  executable M14 player route yet.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m14` embeds the local
  `ptrmj` ROM-set contract with public M14 `maincpu` and `gfx1` region
  placement, exact local filenames, directory-prefixed aliases for the nested
  wrapper, sizes, and CRC32 values. The focused test checks embedded TOML
  synchronization, region/file invariants, and optional real-corpus loading
  through `D:\emu\irem\PT-Reach-Mahjong-Game_Arcade_JA.zip`.
- **Remaining:** implement the NEC D8085AC/8085 board route, M14 memory and I/O
  map, raster/color behavior, paddle/ball/input behavior, sparse discrete or
  sample sound, save-state/player adapter, and visual/audio parity.

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
  output. The board now owns and schedules a second Z80 sound CPU with mapped
  sound ROM/RAM, sound-command latch IRQ/ack state, two native
  YM2149/AY-compatible SSG instances, and one native OKI MSM5205 decoder.
  Command writes only update the latch and IRQ line; focused coverage proves
  AY/MSM state changes only after the sound Z80 executes its modeled port writes.
  Save/load preserves both Z80s, sound RAM, latch state, and audio chip phases;
  the adapter mixes all three captured stereo queues and capability discovery
  exposes both Z80 register snapshots plus the audio register snapshots.
  The compositor now keeps executable ROM and generic work RAM bytes out of the
  direct pixel path, uses the declared control/scroll latches for its remaining
  first-pass background placeholder, and renders 4-byte sprite RAM records
  through the declared 16x16 sprite graphics before the text pass. The text
  pass uses the same final layer plotting path so flip-screen mirrors tile
  positions in addition to glyph pixel order.
  The adapter maps service/test frontend inputs to board-visible active-low
  system bits `0x08`/`0x10` and persists those fields in adapter save-state
  version 1. The parent manifest carries 13 Moon Patrol manual SW1/SW2 DIP
  definitions, the Williams clone inherits them, and the adapter folds the
  active-high factory defaults to `dsw1=0x01` / `dsw2=0x02` while reporting
  `DIP switches=13`. The optional `GLD-M52-PARITY-HASH` data-gated test hashes
  the final RGBA framebuffer and interleaved s16le audio for
  `MNEMOS_M52_PARITY_SET`, but remains skipped until trusted
  `MNEMOS_M52_PARITY_FRAME_SHA256` and/or `MNEMOS_M52_PARITY_AUDIO_SHA256`
  values are supplied.
- **Remaining:** replace the remaining first-pass background placeholder with
  board-evidenced parallax/road/text priority, verify the exact M52 sound CPU port map
  and MSM5205 stream timing against board evidence, implement the
  discrete-analog path beyond the currently modeled AY/MSM port surfaces, add Tropical Angel coverage if M52/M57 evidence confirms the route,
  and prove runtime DIP behavior beyond current manual defaults, raster timing,
  and trusted screenshot/audio parity hashes.

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
  Lot Lot, Spelunker, Lightning Swords, Youjyuden, The Battle-Road, Horizon.
- **Mnemos games:** contract-only raw-media manifests for `battroad`, `horizon`,
  `ldrun`, `ldruna`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`,
  `spelunk2`, and `youjyudn`.
- **Smoke playable:** none. `MNEMOS_M62_SET_DIR=D:\emu\irem` data-gates
  CRC-clean media loading for the eleven local single-inner wrapper ZIPs, but
  there is no executable M62 player route yet.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m62` embeds raw-media ROM-set
  contracts generated from the local Lode Runner, Lot Lot, Spelunker II,
  Battle Road, Horizon, and Youjyuden artifacts. The manifests deliberately map the files
  into a single `raw_media` region so CRCs, sizes, and set grouping are
  preserved without pretending to know the final M62 CPU/video/audio bus
  placement. The focused manifest test checks embedded TOML synchronization,
  region/file invariants, and optional real-corpus loading through nested wrapper
  ZIPs.
- **Remaining:** implement the large Z80/M6803 board family, exact title bus
  maps, dual AY-3-8910, dual MSM5205, KNA custom video behavior, inputs/DIPs,
  save-state/player adapter, and visual/audio parity before promoting any M62
  set from contract-only to smoke playable.

### M63

- **Techsheet games:** Wily Tower, Fighting Basketball.
- **Mnemos games:** contract-only ROM manifest for `wilytowr`.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m63` embeds the local
  `wilytowr` ROM-set contract with public M63 region placement for `maincpu`,
  `soundcpu`, `gfx1`, `gfx2`, `gfx3`, `user1`, and `proms`; the focused
  manifest test checks embedded TOML synchronization, region/file invariants,
  single-inner wrapper loading, directory-prefixed aliases, and CRC-clean local
  loading through `D:\emu\irem\Wily-Tower_Arcade_EN.zip` when
  `MNEMOS_M63_SET_DIR=D:\emu\irem` is provided. The inventory records that
  wrapper as `tracked_contract_only` with `next_action = add_board_profile`.
- **Remaining:** implement the Z80 main board route, 8039-class sound CPU,
  AY/sample/discrete sound path, tile/sprite/video/color-PROM behavior,
  inputs/DIPs, save-state/player adapter, Fighting Basketball coverage, and
  visual/audio parity before counting Wily Tower as playable.

### M72

- **Techsheet games:** R-Type, Ninja Spirit, Image Fight, Legend of Hero Tonma,
  Dragon Breed, X-Multiply, Air Duel, Daiku no Gensan.
- **Mnemos checked-in sets:** `airdueljm72`, `airduelm72`, `bchopper`,
  `dbreedjm72`, `dbreedm72`, `dkgensanm72`, `gallopm72`, `imgfight`,
  `imgfightj`, `imgfightjb`, `loht`, `lohtb2`, `lohtb3`, `lohtj`, `mrheli`,
  `nspirit`, `nspiritj`, `rtype`, `rtypeb`, `rtypej`, `rtypejp`, `rtypeu`,
  `xmultiplm72`.
- **Smoke playable / media-clean local proof:** `airdueljm72`, `airduelm72`,
  `bchopper`, `dbreedjm72`, `dbreedm72`, `dkgensanm72`, `gallopm72`,
  `imgfight`, `imgfightj`, `imgfightjb`, `loht`, `lohtb2`, `lohtb3`, `lohtj`,
  `mrheli`, `nspirit`, `nspiritj`, `rtype`, `rtypeb`, `rtypej`, `rtypejp`,
  `rtypeu`, `xmultiplm72`.
- **Oracle proof:** G6 high-water now records `GLD-M72-RTYPE`,
  `GLD-M72-PROTECTED`, `GLD-M72-PROTECTED-AUDIO`,
  `GLD-M72-PROTECTED-MCU`, and `GLD-M72-VERTICAL` as passed with
  `D:\emu\irem\M72\rtype.zip`, `D:\emu\irem\M72\dbreedm72`,
  `D:\emu\irem\M72\nspirit.zip`, and
  `D:\emu\irem\M72\Air-Duel_Arcade_JA.zip`. `GLD-M72-ROSTER` now passes with
  `MNEMOS_M72_SET_DIR=D:\emu\irem` using its default 900-frame roster window.
  `GLD-M72-PARITY-HASH` now exists as an opt-in final-frame/audio SHA-256
  ratchet and remains skipped until trusted reference hashes are supplied.
- **Dumped-MCU protected proof:** `MNEMOS_M72_PROTECTED_MCU_SET` points at a
  protected true-M72 set that must contain a real MCU dump; the local
  `D:\emu\irem\M72\nspirit.zip` route passes this golden and proves the adapter
  schedules the MCS-51 instead of falling back to no-dump HLE.
- **Rendered-audio smoke proof:** the M72 corpus smoke runner now has an opt-in
  `-RequireRenderedAudio` gate that runs `mnemos_player --extract-audio` and
  requires the exported rendered WAVE payload to contain nonzero PCM.
  `MNEMOS_M72_PROTECTED_AUDIO_SET=D:\emu\irem\M72\dbreedm72` also drives a
  dedicated adapter-level CTest golden that steps the protected no-dump-HLE
  route for 120 frames and requires nonzero rendered PCM. This proves rendered
  audio during the smoke window, but is still weaker than music/audio parity
  certification. `MNEMOS_M72_PARITY_SET` can now pair a trusted ROM path with
  `MNEMOS_M72_PARITY_FRAME_SHA256` and/or `MNEMOS_M72_PARITY_AUDIO_SHA256` to
  turn that parity evidence into a deterministic CTest assertion.
- **Current local artifact proof:** no checked-in M72 manifest artifact is
  currently missing from `D:\emu\irem`: the full M72 artifact preflight reports
  `417/417` present. The optional full-roster CTest is now current proof: with
  `MNEMOS_M72_SET_DIR=D:\emu\irem`, it passes with its default 900-frame window
  through the source-ranking and supplemental-media routes for every checked-in
  true-M72 manifest. The stale unpacked
  `D:\emu\irem\M72\nspirit` folder is incomplete, but the current
  `D:\emu\irem\M72\nspirit.zip` is CRC-complete for both `nspirit` and
  `nspiritj`; the corpus smoke runner now ranks that ZIP ahead of the stale
  same-name folder, preserves manifest-named top-level folders inside an
  exact-stem ZIP, and keeps the parent source when subsetting clone media, so
  the same archive can smoke both routes. The recursive mixed-corpus roster gate
  now finds the local `lohtb3` wrapper under
  `D:\emu\irem\i8751` from the single root `MNEMOS_M72_SET_DIR=D:\emu\irem`;
  the M72 smoke runner's targeted `-Set lohtb3` proof passes through that
  single mixed root with no media-validation issues.
- **Current exact local ZIP evidence:** direct CRC scanning of every entry in
  `D:\emu\irem\M72\gallopm72.zip`,
  `D:\emu\irem\M72\gallop.zip`, and
  `D:\emu\irem\M72\nspirit.zip` now reports `44/44` artifacts present for
  `gallopm72` plus World `nspirit`. Direct ZIP inspection confirms
  `D:\emu\irem\M72\gallopm72.zip` contains `cc_c-pr-.ic1` CRC `0xac4421b1`,
  while `D:\emu\irem\M72\nspirit.zip` contains `nin_c-pr-b.ic1` CRC `0x0f7b2713` and
  `nspiritj/nin_c-pr-.ic1` CRC `0x802d440a`; the scanner reports `48/48`
  present when checking `nspirit` and `nspiritj` from that ZIP. Exact scans of
  `D:\emu\irem\M72\lohtj.zip` plus `D:\emu\irem\M72\loht.zip` report `20/20`
  for `lohtj`, and `D:\emu\irem\M72\lohtb2.zip` plus `D:\emu\irem\M72\loht.zip`
  report `30/30` for `lohtb2`. The player manifest now aliases the local
  `loht.zip` parent/shared filenames used by those clone routes, and targeted
  `gallopm72` / `lohtj` / `lohtb2` corpus smoke passes `3/3`.
- **Current mixed-archive scan evidence:** the M72 artifact scanner now skips
  unreadable entries inside unrelated `.7z` archives and malformed ZIPs instead
  of aborting the corpus walk. The previously failing
  `D:\emu\Chaos Field (English v1.0)[Analog Stick Enabled][cdi].7z` probe now
  completes as a 0/20 non-match, and a rerun across `D:\emu\irem` plus
  `D:\emu\Darksoft Apocalypse M72 2020-12-30.7z` previously reported partial
  blocker counts, but those are superseded by the updated M72 ZIPs. Current live
  proof against `D:\emu\irem` alone reports `94/94` for the prior
  `gallopm72`/`nspirit`/`lohtj`/`lohtb2` blocker group and `417/417` for the
  checked-in M72 manifest preflight.
- **Focused blocker-search evidence:** `scripts/irem_m72/find-missing-artifacts.ps1`
  now accepts `-MissingFromReport <json>` to search only the prior report's
  missing targets and records the seed report path. Size-aware `.7z` listing
  keeps that mode usable without extracting every unrelated archive member.
  Older missing-only scans predate the updated Gallop and Legend of Hero Tonma
  ZIPs and should not be quoted as current blockers.
- **Correct gfx/music:** not certified. The board has the strongest current
  graphics/music implementation, but final visual priority, protection behavior,
  DIP/manual proof, raster phase, and audio parity are still open.
- **Remaining:** finish protected-game behavior with authentic MCU artifacts or
  bounded no-dump HLE, complete the full roster corpus, prove board-manual DIP
  behavior, and populate/pass trusted screenshot/audio parity hashes.

### M75

- **Techsheet games:** Vigilante.
- **Mnemos games:** `vigilant`, `vigilanta`, `vigilantb`, `vigilantbl`,
  `vigilantc`, `vigilantd`, `vigilantg`, `vigilanto`.
- **Smoke playable:** the complete local parent wrapper
  `D:\emu\irem\Vigilante_Arcade_EN (3).zip` unwraps to `vigilant.zip`, and the
  official regional clone wrappers `D:\emu\irem\Vigilante_Arcade_EN (1).zip`
  through `D:\emu\irem\Vigilante_Arcade_EN (6).zip`,
  `D:\emu\irem\Vigilante_Arcade_JA.zip`, and the bootleg wrapper
  `D:\emu\irem\Vigilante_Arcade_EN.zip` load CRC-clean through clone/parent
  fallback. The data-gated corpus test steps all eight M75 manifests, produces
  nonblank diagnostic output, and supports save/load;
  `D:\emu\irem\Vigilante_Arcade_EN.zip` also has direct 256x256 nonblank
  screenshot smoke evidence.
- **Correct gfx/music:** none. The board has a first-pass diagnostic video path
  and executable Z80/Z80/YM2151/DAC ownership, including synthetic
  sample-ROM-read-to-DAC proof on the sound Z80 elapsed-clock timeline, not
  authentic Vigilante graphics/music certification.
- **Current implementation:** `src/manifests/irem_m75` owns a Z80 main CPU, Z80
  sound CPU, YM2151, DAC, 16-bit Z80 memory buses, Vigilante ROM banking, RAM
  windows, inputs/DIPs, sound latch/ack, sample-address/DAC ports, the two-bank
  5-bit KNA91-style palette bus, rear color/disable register semantics,
  whole-board save/load identity, 14 Vigilante manual SW1/SW2 DIP definitions
  folded to active-low defaults `dsw1=0xff` / `dsw2=0xfd`, and embedded
  parent/clone Vigilante ROM contracts including `vigilantbl` bootleg PROM/PAL
  media. The M75 system tests now prove the
  sound Z80 can program the sample cursor, read consecutive sample ROM bytes
  through the modeled port, write them through the DAC as ordered
  sound-clocked events, and acknowledge the latch.
  The player adapter maps service/test frontend inputs to the board-visible
  active-low system bits `0x10`/`0x20`, persists those fields in adapter
  save-state version 2, and reports `DIP switches=14` from the parsed manifest.
  `src/apps/player` registers `--system irem_m75` and alias `m75`, supports direct ZIPs,
  single-inner wrapper ZIPs, unpacked folders, in-archive `game.toml`, clone/parent
  fallback media resolution, resident media validation, rollback-ready save-state, capability discovery, and
  `MNEMOS_M75_SET_DIR=D:\emu\irem` corpus gating.
- **Remaining:** replace the diagnostic compositor with board-evidenced
  Vigilante background/foreground/sprite priority, verify exact memory/I/O,
  DIP runtime behavior beyond current manual defaults, and raster phase, prove sound CPU sample/DAC
  timing against board evidence, prove bootleg-specific PROM/color behavior, and
  collect screenshot/audio parity evidence before marking graphics or music
  correct.

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
  routes with sound-Z80-clocked DAC event proof, not final parity.
- **Remaining:** verify or replace first-pass video priority, raster timing,
  DIP behavior, palette-bank rendering/decode, board timing, and visual/audio
  parity.

### M82

- **Techsheet games:** Major Title, plus M82 builds of Air Duel and Daiku no
  Gensan.
- **Mnemos games:** `airduel`, `airduelu`, `majtitle`, `majtitlej`, `rtype2`,
  `rtype2j`, `rtype2jc`, `rtype2m82b`.
- **Smoke playable:** all eight checked-in local M82 sets load through `--system
  irem_m82` with clone-parent fallback where needed and nonblank player smoke.
  Air Duel uses `D:\emu\irem\M72\Air-Duel_Arcade_EN (1).zip` for the M82 parent
  and `D:\emu\irem\M72\Air-Duel_Arcade_EN (2).zip` for the US split clone,
  while the duplicate mixed `D:\emu\irem\M72\airduel.zip` remains loadable by
  explicit `--system irem_m82`.
  Major Title uses the local `D:\emu\irem\Major-Title_Arcade_EN.zip` parent
  wrapper and `D:\emu\irem\Major-Title_Arcade_JA.zip` Japan split wrapper.
  Its dedicated `backgrounds` ROM region is now loaded and consumed by the rear
  tilemap renderer, with a focused empty-foreground regression test covering that
  path.
- **Correct gfx/music:** not certified. Current audio now proves
  sound-Z80-clocked DAC event ordering, but final music/audio parity remains
  open.
- **Classification note:** the factsheet places R-Type II under M84, while the
  current Mnemos implementation routes the local R-Type II sets through
  `irem_m82`. Keep this as an explicit board-evidence audit item rather than
  treating either label as final.
- **Remaining:** prove board classification, Major Title/Air Duel background
  priority/parity against reference evidence, palette-bank rendering/decode,
  exact raster phase, DIP behavior, visual-priority parity, and audio parity.

### M84

- **Techsheet games:** R-Type II, Hammerin' Harry / Daiku no Gensan, Cosmic Cop,
  Ken-Go.
- **Mnemos games:** `cosmccop`, `gallop`, `hharryb`, `hharryu`, `ltswords`.
- **Smoke playable:** both local split sets load through `--system irem_m84` when
  composed with the M81 `hharry` parent media. The local unpacked
  `D:\emu\irem\M72\ltswords` folder now loads as a standalone M84 V35-profile
  set using CRC-verified program, sound, graphics, and sample regions.
  `D:\emu\irem\M72\gallop.zip` loads as standalone M84 V35-profile Gallop /
  Cosmic Cop media with CRC-verified program, sound, graphics, samples, PROMs,
  and PLDs. `D:\emu\irem\M72\cosmccop.zip` now loads as the Cosmic Cop clone,
  using its own program/sound/graphics/sample dumps and inheriting Gallop's
  PROM/PLD artifacts. The Gallop manifest also carries 10 DIP switch
  definitions, and the adapter applies their composed default (`0xf9bf`) to the
  board DIP register for both Gallop and Cosmic Cop.
- **Correct gfx/music:** not certified.
- **Local corpus note:** `ltswords` and `gallop.zip` are stored under the `M72`
  corpus bucket, but their Mnemos manifests track them as M84. The current
  `ltswords` folder lacks the small PROM/PLD artifacts listed for the complete
  board; the manifest carries an explicit `irem_m84_prom_pld` HLE declaration
  for that first-pass route. The same bucket also contains a `gallop` unpacked
  folder with different true-M72-style filenames; the proved M84 Gallop route is
  the complete `D:\emu\irem\M72\gallop.zip` archive with manifest-backed DIP
  defaults, and `D:\emu\irem\M72\cosmccop.zip` is a clone route that uses
  `gallop.zip` as its parent for PROM/PLD inheritance.
- **Remaining:** replace or verify the M81-compatible wrapper assumptions with
  board evidence, resolve the R-Type II classification mismatch, recover/prove
  the `ltswords` PROM/PLD artifacts, and prove M84-specific memory/I/O,
  priority, raster timing, DIP behavior beyond current manifest defaults, and
  screenshot/audio parity.

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
  save-state bytes. The adapter also maps P1/P2 service plus operator-test
  inputs to the board-visible active-low system port, preserves them across
  save/load, retains parsed manifest DIP metadata, folds parsed DIP defaults
  into the 16-bit board DIP register, exposes `DIP switches` when manifests
  provide switch tables, and still honors explicit `--dip` overrides.
- **Correct gfx/music:** not certified. The board shell now matches the
  V35/Z80/YM2151/DAC topology, but rendering is a GA25 diagnostic compositor and
  audio proof is limited to the Z80/YM/DAC route plus sound-Z80-clocked
  synthetic DAC mixing.
- **Local corpus note:** `Atomic-Punk_Arcade_EN.zip` currently lives under the
  `D:\emu\irem\M72` storage bucket and unwraps to `atompunk.zip`; the three
  `New-Atomic-Punk-Global-Quest_*` root wrappers unwrap to `newapunk`,
  `bbmanwj`, and `bbmanwja`. The available local wrappers do not include a
  complete GA25 graphics ROM set, so they are route/protection/audio evidence,
  not final visual parity evidence.
- **Remaining:** authentic GA25 tile/sprite/row-scroll behavior, V35 on-die
  interrupt/timer behavior, complete Bomber Man World graphics media, Hasamu and
  Quiz F-1 manifests/corpus proof, board-authentic DIP tables/runtime behavior,
  and screenshot/audio parity.

### M92

- **Techsheet games:** Gunforce, R-Type Leo, In the Hunt, Undercover Cops,
  Ninja Baseball Bat Man, Blade Master, Mystic Riders, Major Title 2, Hook,
  Superior/Perfect Soldiers, Gunforce 2.
- **Mnemos games:** `bmaster`, `crossbld`, `geostorm`, `gunforce`,
  `gunforcej`, `gunforceu`, `gunforc2`, `gunhohki`, `hook`, `inthunt`,
  `inthuntu`, `mysticri`, `mysticrib`, `nbbatman`, and `nbbatmanu`
  as checked-in manifests under `irem_m92`.
- **Smoke playable:** all fifteen local wrapper ZIPs load through `--system
  irem_m92`/the M92 adapter data gate, step one frame, produce a 320x240
  nonblank diagnostic frame, and produce save-state bytes. `crossbld`,
  `geostorm`, `inthuntu`, `gunforcej`, `gunforceu`, `mysticri`, `gunhohki`,
  `mysticrib`, `nbbatman`, and `nbbatmanu` also have direct `mnemos_player`
  screenshot/save-state smokes.
- **Correct gfx/music:** not certified. Current video is a diagnostic
  region/RAM/PLD-driven first-pass compositor. Current sound proves the
  YM2151/GA20 shell, synthetic GA20 MMIO, and a synthetic V33-to-V35
  command/reply latch path with save-state persistence. Main V33 command writes
  now assert the modeled V35 command IRQ through INTP1/vector 25, latch reads do
  not acknowledge it, sound-side writes to `$a8044` acknowledge it, and YM2151
  Timer A dispatches through INTP0/vector 24. Simultaneous pending YM/command
  IRQ proof selects INTP0 before INTP1 and then services the still-pending
  command IRQ after the YM source clears. The V30/V33/V35 core now fetches
  instruction bytes through the bus opcode path, and M92 can map an optional
  `soundcpu_opcodes` decrypted V35 opcode image while data reads still see the
  raw encrypted `soundcpu` ROM. This is still not the proprietary M92 V35
  decrypt transform/key, cycle-exact interrupt latency, or audio parity.
- **Local corpus note:** fifteen local M92-era title-wrapper ZIPs now resolve to
  embedded set IDs and load CRC-clean through `MNEMOS_M92_SET_DIR`: Blade Master
  parent/Japan clone (`bmaster`, `crossbld`), Gunforce parent
  (`gunforce`), Gunforce Japan/US split clones (`gunforcej`, `gunforceu`) via
  parent fallback, Gunforce 2 / Geostorm (`gunforc2`, `geostorm`), Hook
  (`hook`), In the Hunt parent/US clone (`inthunt`, `inthuntu`), Mystic Riders
  (`mysticri`), Gun Hohki (`gunhohki`), the Mystic Riders split bootleg route
  (`mysticrib`), Ninja Baseball Bat Man (`nbbatman`), and the US split clone
  (`nbbatmanu`) via parent fallback. In the current sorted corpus, older M92
  routes live under
  `D:\emu\irem\M72` while the Mystic Riders and Ninja Baseball wrappers live at
  `D:\emu\irem`; both are storage artifacts rather than board proof.
- **Remaining:** derive/verify the proprietary M92 V35 decrypt transform/key,
  cycle-exact V35 interrupt latency, exact GA20/YM2151 sound protocol,
  GA21/GA22 video/priority behavior, exact memory/I/O maps, protection details,
  DIP/raster behavior, and visual/audio parity proof.

### M107

- **Techsheet games:** Fire Barrel, Dream Soccer '94, World PK Soccer.
- **Mnemos games:** `airass`, `firebarr`.
- **Smoke playable:** both sets are data-gated through `MNEMOS_M107_SET_DIR`;
  Air Assault has direct nonblank screenshot and save/load smoke. Fire Barrel is
  CRC-clean and player-routable, but not parity-certified. Both manifests carry
  the shared Fire Barrel / Air Assault SW1/SW2 and SW3 DIP profile; the adapter
  applies the composed SW1/SW2 default (`0xffbf`), applies the separate SW3
  `COINS_DSW3` default (`0xebff`), exposes 12 DIP definitions in system spec,
  and maps service/test frontend inputs to the M107 `COINS_DSW3` service-credit
  and operator-service bits.
- **Correct gfx/music:** not certified. Current rendering is an M107-local
  diagnostic path; GA20 exists, the command/reply latch now has pending-state,
  V35 INTP1/vector-25 dispatch, explicit acknowledge, reply proof, and YM2151
  Timer A INTP0/vector-24 dispatch proof plus simultaneous pending YM/command
  arbitration proof selecting INTP0 before INTP1, followed by proof that a
  still-pending command IRQ is serviced through INTP1 after the YM2151 source is
  cleared. The modeled main/sound windows match the current board evidence for
  work RAM, VRAM, sprite RAM, palette RAM, sound RAM, GA20, YM2151, command
  latch, and sound reply. Cycle-exact V35 interrupt latency and analog
  balance/filtering are not proven.
- **Remaining:** V33/V35-specific timing and on-die peripheral proof, deeper
  M107 I/O behavior, GA21/GA22 video and priority behavior, cycle-exact V35
  interrupt latency, GA20 analog mix, raster timing, and screenshot/audio
  parity.

### M119

- **Techsheet games:** none confidently listed.
- **Mnemos games:** none.
- **Smoke playable:** none.
- **Correct gfx/music:** none.
- **Remaining:** sparse-board research before implementation.

## Immediate Closure Candidates

1. Supply trusted visual/audio parity hashes for the new M52 hash oracle and
   the existing M72 hash oracle before promoting any game to "correct gfx/music".
2. Advance M52 Moon Patrol from first-pass route to authentic video/audio by
   replacing the diagnostic compositor and first-pass digital audio with board-evidenced
   parallax, road, sprite, sound-CPU-owned MSM5205 stream timing, and discrete
   analog behavior.
3. Resolve the M82/M84 R-Type II classification mismatch with board evidence and
   adjust manifests/docs if needed.
4. Use the now-passing M72 roster golden as the baseline for the next
   protection/DIP/parity slices; the remaining M72 work is not missing media but
   stronger authenticity proof.
5. Promote M63 Wily Tower from ROM contract to an executable board/profile route
   before counting it as smoke playable.
6. Promote M62 from raw-media contracts to a real board/profile route before
   counting any Lode Runner, Spelunker II, Battle Road, or Youjyuden set as
   smoke playable.
7. Use `scripts\irem\run-local-corpus.ps1 -IncludeFullM72Roster` for the strict
   M72 roster proof. With the switch, the runner prints a checked-in-manifest
   artifact preflight before CTest; without the switch it is the available-artifact proof
   runner for every implemented Irem family.
8. Advance M90 from a diagnostic V35/Z80/YM/DAC shell to authentic GA25 video
   once complete graphics media and board evidence are available.
9. Advance the M92 first-pass profile from diagnostic execution to authenticity
   by resolving encrypted V35 sound-CPU behavior and GA21/GA22 video evidence.
