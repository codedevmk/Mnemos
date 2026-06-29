# Irem Mnemos Implementation Inventory

Generated on 2026-06-29 from the Irem board factsheet and the current
`feature/irem-arcade` worktree.

Primary board taxonomy source:
`docs/architecture/factsheets/irem-system-boards-reference.md`.

Current Mnemos coverage sources:
`src/manifests/irem_*`, `src/apps/player/adapters/irem_*`,
`docs/parity-gap-inventory.md`, and the local metadata command:

```powershell
scripts\irem\inventory-corpus.ps1 -Root D:\emu\irem -Recurse -Out build\scratch\irem-implementation-inventory-corpus.json
```

That scan found 437 local corpus items across 24 top-level buckets. Direct files
under `D:\emu\irem` are zero; the remaining files live under board/system
buckets such as `M72`, `M92`, `M62`, `M58`, and `travrusa`, plus quarantine or
classification buckets such as `for-delete`, `misc`, and `non-irem`. The
lowercase `travrusa` bucket is intentional: Mnemos currently treats Traverse
USA / Zippy Race as its own first-pass family profile because the factsheet does
not yet map it to a numbered M-board. Of those 437 items, 282 currently match a
checked-in Mnemos Irem manifest, 154 are readable through the current ZIP,
single-inner wrapper ZIP, or unpacked-folder media routes, and 154 have an
executable player-supported route. M57 now has a first-pass New Tropical Angel
player route; `newtangl.zip` is supported while `newtangl.7z` remains
metadata-only until converted or unpacked. M63 also has a first-pass Wily Tower
player route; `wilytowr.zip` is supported while the two local `.7z` Wily Tower archives
remain metadata-only until converted or unpacked. No tracked local item is
currently contract-only; ignored
buckets may still show filename-level manifest matches, but they contribute zero
tracked, loadable, supported, contract-only, or metadata-only support counts.
Board-local `name-collisions` folders are skipped by both inventory and
data-gated corpus source discovery.
Windows copy-suffixed checked-in set ZIPs such as `loht (1).zip` are
canonicalized to their embedded manifest IDs for player loading, M72
corpus-smoke grouping, and inventory grouping. The M14 bucket now has a
first-pass player route for `ptrmj`: two local ZIP routes count as supported,
while `ptrmj.7z` remains metadata-only until converted or unpacked. The M27 bucket now has a
first-pass player route for `panther`: two local ZIP routes count as supported,
while `panther.7z` remains metadata-only until converted or unpacked. The M47 bucket now has a
first-pass player route for `olibochu` and `punchkid`: two local ZIP routes count
as supported, while `punchkid.7z` remains metadata-only until converted or
unpacked. The `travrusa` bucket has checked-in contracts and a first-pass player
route for `travrusa`, `motorace`, `travrusab`, and `travrusab2`; four local
travrusa ZIP routes count as supported, while the two `.7z` archives and the
unsuffixed artwork/layout `travrusa.zip` remain metadata-only for ROM support
accounting. Artwork/layout-only ZIPs are classified as
`non_rom_artwork_package` and do not count as player-loadable ROM proof. Known
untracked corpus classifications remain explicit: `headon` and
`uniwars` / `uniwarsa` are non-Irem reference sets from `sega/vicdual.cpp` and
`galaxian/galaxian.cpp`.
The common data-gated runner now includes the M57 and M63 player corpus proofs
plus G6-ratcheted corpus golden tests for every implemented Irem player family:
M14, M15, M27, M47, M52, M57, M58, M62, M63, travrusa, M72, M75, M81, M82, M84,
M85, M90, M92, and M107. For
the current Windows local corpus layout, `scripts\irem\run-local-corpus.ps1`
wires board-specific folders under `D:\emu\irem` into those data-gated tests.
The strict full-M72 roster gate remains opt-in because it is a data-heavy player
proof.

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
| M14 | `irem_m14` first-pass | 16% | `ptrmj` | local P.T. Reach Mahjong ZIPs through the adapter; direct `mnemos_player --system irem_m14` nonblank screenshot and `--system m14` save-state proof | None | Authentic NEC D8085AC/8085 CPU timing instead of the temporary 8080-compatible surrogate, video/color, paddle/mahjong/input behavior, discrete/sample sound, visual/audio parity |
| M27 | `irem_m27` first-pass | 18% | `panther` | local Panther ZIPs through the adapter; direct `mnemos_player --system irem_m27` nonblank screenshot and `--system m27` save-state proof | None | Authentic M27 memory/I/O timing, bitmap/char video and color behavior, input/DIP behavior, Panther audio-board behavior, raster/audio/video parity |
| M47 | `irem_m47` first-pass | 22% | `olibochu`, `punchkid` | local Oli-Boo-Chu parent and Punching Kid split clone ZIPs; direct `mnemos_player --system irem_m47` nonblank screenshot and `--system m47` save-state proof | None | Authentic M47 memory/I/O timing, video/color PROM behavior, AY/sample sound timing, input/DIP parity, visual/audio parity |
| M52 | `irem_m52` first-pass | 42% | `mpatrol`, `mpatrolw` | local Moon Patrol wrappers; service/test input proof; manual-backed DIP defaults; sound-Z80-owned AY/MSM write proof; RAM/GFX-backed sprite pass; text flip-screen position proof; optional visual/audio hash oracle | None | Authentic parallax/road/background priority, exact sound CPU port/protocol timing, discrete analog path, Moon Patrol / Tropical Angel board-split proof, DIP runtime/parity behavior, pinned raster/audio/video parity hashes |
| M57 | `irem_m57` first-pass raw-media route | 12% | `newtangl` | local New Tropical Angel ZIP through the adapter; direct `mnemos_player --system irem_m57` nonblank screenshot and `--system m57` save-state proof | None | Authentic M57 memory/I/O timing, video/color, Irem Audio, inputs/DIPs, visual/audio parity |
| M58 | `irem_m58` first-pass | 28% | `10yard`, `10yardj`, `vs10yard`, `vs10yardj` | all 4 local ZIP sets through the adapter; real `soundcpu` reset vector proves the MC6803 high-ROM path; direct `mnemos_player --system irem_m58` nonblank screenshot and `--system m58` save-state proof for parent/Japan sets | None | Authentic 10-Yard Fight memory/I/O timing, video priority/color/radar details, exact MC6803 port/timer/audio timing, DIP/manual behavior, visual/audio parity hashes |
| M62 | `irem_m62` first-pass Lode Runner/Lot Lot/Spelunker II MC6803/SSG/MSM5205 route plus raw-media and clone-parent fallback | 31% | `battroad`, `bkungfu`, `horizon`, `kidniki`, `kungfum`, `ldrun`, `ldruna`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, `spelunk2`, `spartanx`, `yanchamr`, `youjyudn` | all 16 checked-in exact local ZIP set routes through the adapter; `ldrun`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, and `spelunk2` region contracts load Z80 program, MC6803 sound ROM, graphics, PROM, and timing regions with the MC6803 reset vector proven in the `$8000-$ffff` sound-ROM window; direct `mnemos_player --system irem_m62` nonblank screenshot/save-state proof for `ldrun`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, and `spelunk2` | None | Exact M62 title bus maps, MC6803 port/timer timing, exact dual MSM5205 stream/control timing, KNA custom video, inputs/DIPs, visual/audio parity |
| M63 | `irem_m63` first-pass | 14% | `wilytowr` | local Wily Tower ZIP through the adapter; direct `mnemos_player --system irem_m63` nonblank screenshot and `--system m63` save-state proof | None | Authentic Z80 + 8039/AY/sample board profile, video/color PROM path, Fighting Basketball manifest, input/DIP behavior, visual/audio parity |
| Traverse USA / Zippy Race | `irem_travrusa` first-pass | 23% | `travrusa`, `motorace`, `travrusab`, `travrusab2` | local parent/copy-suffixed parent and split wrappers; direct `mnemos_player --system irem_travrusa` nonblank screenshot and `--system travrusa` save-state proof | None | Authentic MotoRace encrypted ROM handling, exact Irem Audio timing, video priority/scroll/color/input behavior, visual/audio parity |
| M72 | `irem_m72` | 70% | 23 checked-in manifests | all 23 checked-in sets are media-clean smoke-proven; `dbreedm72` also has nonzero rendered-audio smoke proof | None | Remaining MCU/protection artifacts, no-dump HLE depth, DIP/manual proof, visual/audio parity |
| M75 | `irem_m75` first-pass | 34% | `vigilant`, `vigilanta`, `vigilantb`, `vigilantbl`, `vigilantc`, `vigilantd`, `vigilantg`, `vigilanto` | local Vigilante parent plus official regional and bootleg clone wrappers; service/test input proof; manual-backed DIP defaults; sound-Z80-clocked DAC event proof | None | Authentic Vigilante graphics priority, DIP runtime UI/override parity, raster phase, reference-backed sound timing, audio parity, bootleg PROM/color behavior proof |
| M77 | none | 0% | None | None | None | Board research before implementation |
| M81 | `irem_m81` | 56% | `dbreed`, `hharry`, `xmultipl` | all 3 local sets; sound-Z80-clocked DAC event proof | None | Video priority, raster timing, DIP proof, palette-bank decode, visual/audio parity |
| M82 | `irem_m82` | 68% | `airduel`, `airduelu`, `majtitle`, `majtitlej`, `rtype2`, `rtype2j`, `rtype2jc`, `rtype2m82b` | all 8 checked-in local sets; local Air Duel M82 parent/US clone wrappers; sound-Z80-clocked DAC event proof | None | Board classification audit, Major Title/Air Duel priority/parity proof, palette-bank decode, raster phase, DIP proof, priority parity, audio parity |
| M84 | `irem_m84` wrapper | 48% | `cosmccop`, `dkgensan`, `dkgensana`, `gallop`, `hharryb`, `hharryu`, `kengo`, `kengoj`, `ltswords` | all 9 checked-in M84 sets; Daiku no Gensan and Ken-Go split-clone parent fallback; V30/V35 CPU profile proof; Gallop/Cosmic Cop DIP default `0xf9bf` | None | Replace M81-compatible assumptions, M84 memory/I/O, Hammerin' Harry/Cosmic Cop/Ken-Go priority/raster, board-authentic DIP proof, `ltswords`/Ken-Go PROM/PLD artifacts |
| M85 | `irem_m85` wrapper | 22% | `poundfor`, `poundforj` | local parent and Japan split-clone ZIPs load CRC-clean through `--system irem_m85` / `m85`; nonblank screenshot and save-state proof | None | Replace M81-compatible assumptions, prove M85 memory/I/O/video/audio/input/DIP behavior, visual/audio parity |
| M90 / M97 / M99 | `irem_m90` first-pass | 36% | `atompunk`, `bbmanw`, `bbmanwj`, `bbmanwja`, `gussun`, `hasamu`, `newapunk`, `quizf1`, `riskchal` | all 9 local M90 ZIPs under `D:\emu\irem\M90`; split-clone parent fallback for Atomic Punk/Bomber Man World/Gussun; service/test input proof; parsed DIP metadata support; sound-Z80-clocked DAC event proof; resident GA25 graphics/sample media validation where dumped | None | Authentic GA25 video, V35 on-die peripherals and banked program mapping, board-authentic DIP tables/runtime proof, visual/audio parity |
| M92 | `irem_m92` | 50% first-pass | `bmaster`, `crossbld`, `geostorm`, `gunforce`, `gunforcej`, `gunforceu`, `gunforc2`, `gunhohki`, `hook`, `inthunt`, `inthuntu`, `lethalth`, `mysticri`, `mysticrib`, `nbbatman`, `nbbatmanu`, `rtypeleo`, `rtypeleoj`, `thndblst`, `uccops`, `uccopsar`, `uccopsj`, `uccopsu` | all 23 data-gated first-pass sets; Blade Master Japan, Geostorm, In the Hunt US, GunForce, Lethal Thunder/Thunder Blaster, Mystic Riders, Ninja Baseball Bat Man, R-Type Leo, and Undercover Cops nonblank/save-state smokes; modeled V35 command/YM IRQ priority proof | None | Encrypted V35 sound CPU behavior/decryption, GA21/GA22 video/priority, exact M92 memory/I/O, protection, DIP/raster/audio/video parity |
| M107 | `irem_m107` | 58% | `airass`, `dsoccr94`, `firebarr` | all 3 checked-in sets are data-gated; Air Assault and Dream Soccer '94 have direct nonblank/save-state smoke; Fire Barrel is CRC-clean and player-routable; shared Fire Barrel/Air Assault SW1/SW2 default `0xffbf` and SW3 `COINS_DSW3` default `0xebff`; Dream Soccer SW3 Player Power default keeps `COINS_DSW3=0xffff`; service/test plus command/YM IRQ priority proof | None | V33/V35-specific behavior, deeper M107 I/O proof, GA21/GA22 video, cycle-exact V35 IRQ latency/GA20 analog mix, raster/parity |
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
- M14 now has a P.T. Reach Mahjong ROM contract plus a first-pass executable
  Z80-backed 8080-compatible surrogate route. `MNEMOS_M14_SET_DIR=D:\emu\irem\M14`
  proves the local ZIPs through the adapter. Current graphics and audio remain
  diagnostic first-pass output, not board-authentic M14 parity.
- M27 now has a Panther ROM contract plus a first-pass executable MOS 6502
  player route. `MNEMOS_M27_SET_DIR=D:\emu\irem\M27` proves the local ZIPs
  through the adapter. Current graphics and audio remain diagnostic first-pass
  output, not board-authentic Panther parity.
- M47 now has Oli-Boo-Chu and Punching Kid ROM contracts plus a first-pass
  executable Z80/Z80/two-SSG player route. `MNEMOS_M47_SET_DIR=D:\emu\irem\M47`
  proves both local ZIP sets through the adapter; direct `mnemos_player --system
  irem_m47` screenshot and `--system m47` save-state smokes prove the
  user-facing launch path. Current graphics and audio remain diagnostic
  first-pass output, not board-authentic M47 parity.
- M58 now has four 10-Yard Fight ROM-set contracts plus a first-pass executable
  Z80/MC6803/two-SSG player route. `src/manifests/irem_m58` maps the `soundcpu`
  high ROM window at `$8000-$ffff`, resets the MC6803 through the real
  `$fffe/$ffff` vector, and routes the existing latch/SSG proof through
  first-pass direct-page MC6803 MMIO instead of Z80 ports. `MNEMOS_M58_SET_DIR=D:\emu\irem\M58`
  proves the four local ZIP sets through the adapter; direct `mnemos_player
  --system irem_m58` screenshot and `--system m58` save-state smokes prove the
  user-facing launch path. The `D:\emu\irem\M58\10yard (2).zip` artwork/layout
  package is not ROM evidence and remains quarantined under
  `D:\emu\irem\M58\artwork`.
- M62 now has sixteen ROM-set contracts plus a first-pass player route with
  CRC-clean local wrapper-ZIP load proof, nonblank screenshot proof, and
  save-state proof. `ldrun`, `ldrun2`, `ldrun3`, and the parent-backed
  `ldrun3j` clone have been promoted from raw-media-only staging to explicit
  Z80 program, MC6803 sound ROM, graphics, PROM, and timing regions, and the
  board route wires a first-pass MC6803 direct-page latch/dual-SSG path for the
  Lode Runner family.
  M62 remains smoke-playable only, not authentic KNA/MSM5205/audio-video parity.
- M63 now has a single `wilytowr` ROM contract plus a first-pass executable
  Z80 diagnostic player route. `MNEMOS_M63_SET_DIR=D:\emu\irem\M63` proves the
  local ZIP through the adapter; direct `mnemos_player --system irem_m63`
  screenshot and `--system m63` save-state smokes prove the user-facing launch
  path. Current graphics and audio remain diagnostic first-pass output, not
  board-authentic M63 parity.
- M82 has scanline-composed tile/sprite/palette rendering with focused priority
  tests, four R-Type II set routes, and Major Title parent/Japan wrapper routes;
  Major Title's dedicated background ROM region now feeds the rear tilemap when
  present.
- M15, M47, M58, M75, M81, M84, M85, M90, M92, and M107 all have player-routable first-pass boards with
  nonblank local smoke evidence, but each still has explicit authenticity gaps.
  M75 currently covers the complete local Vigilante parent wrapper plus official
  regional and bootleg clone wrappers with a Z80/Z80/YM2151/DAC first-pass route,
  service/test input proof, and Vigilante manual-backed DIP defaults.
  M92 currently covers the complete local GunForce parent wrapper plus the local
  US/Japan split clone wrappers, Blade Master parent/Japan clone, Gunforce 2 /
  Geostorm, In the Hunt parent/US clone, Lethal Thunder parent/Japan clone,
  the Mystic Riders parent/Japan/bootleg wrappers through clone-parent fallback,
  Ninja Baseball Bat Man parent/US split-wrapper routes, R-Type Leo
  parent/Japan split-wrapper routes, and Undercover Cops parent/US/Japan/Alpha
  Renewal routes.
  M84 now includes V30-profile Hammerin' Harry / Daiku no Gensan split-clone
  routes and V35-profile `ltswords`, `kengo`, `kengoj`, `gallop`, and
  `cosmccop` routes; `ltswords` and its Ken-Go clones declare missing PROM/PLD
  artifacts, while `gallop.zip` supplies the complete listed PROM/PLD set and
  `cosmccop.zip` inherits those artifacts through the Gallop parent, but all
  routes still need board-parity evidence.
  M85 now includes first-pass Pound for Pound parent and Japan split-clone
  routes with CRC-clean parent fallback and direct player screenshot/save-state
  proof, but it still uses a compatibility core pending board-specific proof.

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
- **Mnemos games:** first-pass ROM manifest/player route for `ptrmj`.
- **Smoke playable:** the local `ptrmj` ZIP wrappers under `D:\emu\irem\M14`
  now match the checked-in M14 manifest and run through
  `src/apps/player/adapters/irem_m14`. `MNEMOS_M14_SET_DIR=D:\emu\irem\M14`
  data-gates CRC-clean loading, player stepping, nonblank diagnostic
  framebuffer output, and save-state creation. Direct proof includes
  `mnemos_player --system irem_m14 --rom D:\emu\irem\M14\ptrmj.zip --frames 90
  --screenshot build\scratch\irem-m14\ptrmj.ppm`, which wrote a 256x256
  nonblank PPM, and `mnemos_player --system m14 --rom
  D:\emu\irem\M14\ptrmj.zip --frames 90 --save-state
  build\scratch\irem-m14\ptrmj.mstate`, which wrote a 22806-byte save state.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m14` embeds the local
  `ptrmj` ROM-set contract with public M14 `maincpu` and `gfx1` region
  placement, exact local filenames, directory-prefixed aliases for the nested
  wrapper, sizes, and CRC32 values. The focused test checks embedded TOML
  synchronization, region/file invariants, and optional real-corpus loading
  through `D:\emu\irem\M14`. `m14_system.cpp` adds a first-pass board shell with
  a Z80 core used as an 8080-compatible surrogate, program ROM at `$0000`, RAM,
  video, color, work, input/DIP/control MMIO, I/O-port mirroring for early CPU
  code, a GFX-ROM-driven diagnostic compositor, a beeper-backed sound latch,
  save-state identity, and player registration under `irem_m14` / `m14`.
- **Remaining:** replace the surrogate with authentic NEC D8085AC/8085 CPU
  timing and any 8085-specific I/O behavior, prove the real M14 memory and I/O
  map, raster/color behavior, paddle/mahjong/input behavior, sparse discrete or
  sample sound, and visual/audio parity.

### M27

- **Techsheet games:** Panther.
- **Mnemos games:** first-pass ROM manifest/player route for `panther`.
- **Smoke playable:** the local `panther` ZIP wrappers under `D:\emu\irem\M27`
  now match the checked-in M27 manifest and run through `src/apps/player/adapters/irem_m27`.
  `MNEMOS_M27_SET_DIR=D:\emu\irem\M27` data-gates CRC-clean loading, player
  stepping, nonblank diagnostic framebuffer output, and save-state creation.
  Direct proof includes `mnemos_player --system irem_m27 --rom
  D:\emu\irem\M27\panther.zip --frames 90 --screenshot
  build\scratch\irem-m27\panther.ppm`, which wrote a 256x256 nonblank PPM, and
  `mnemos_player --system m27 --rom D:\emu\irem\M27\panther.zip --frames 90
  --save-state build\scratch\irem-m27\panther.mstate`, which wrote a 1535-byte
  save state.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m27` embeds the local
  `panther` ROM-set contract with public M27 `maincpu`, `audiocpu`, and `proms`
  region placement: seven 2 KiB M6502 program ROMs at `$8000-$B7FF`, one
  Panther audio-board ROM at `$7000`, and the 512-byte color PROM.
  `src/manifests/irem_m27/m27_system.cpp` adds a first-pass MOS 6502 board shell
  with RAM, PROM/RAM diagnostic video, input/DIP latches, a beeper-backed sound
  latch, save-state identity, and `--system irem_m27` / `m27` player
  registration. The focused tests check embedded TOML synchronization, exact
  file offsets/CRCs, region/file invariants, synthetic CPU/video/audio behavior,
  save/load identity, and optional real-corpus loading through `D:\emu\irem\M27`.
- **Remaining:** replace first-pass timing and diagnostic video/audio with the
  authentic M27 CPU memory/I/O map, bitmap/char video and color behavior,
  input/DIP behavior, Panther audio-board behavior, raster phase, and
  visual/audio parity.

### M47

- **Techsheet games:** Oli-Boo-Chu, Punching Kid.
- **Mnemos games:** first-pass ROM manifests/player route for `olibochu` and
  `punchkid`.
- **Smoke playable:** the local ZIP wrappers under `D:\emu\irem\M47` now match
  checked-in M47 manifests and run through the player adapter. `punchkid`
  resolves shared audio, sample, 16x16 graphics, and PROM dumps from the sibling
  `olibochu` parent wrapper. Direct proof includes `mnemos_player --system
  irem_m47 --rom D:\emu\irem\M47\olibochu.zip --frames 90 --screenshot
  build\scratch\irem-m47\olibochu.ppm`, which wrote a 256x256 nonblack PPM, and
  `mnemos_player --system m47 --rom D:\emu\irem\M47\punchkid.zip --frames 90
  --save-state build\scratch\irem-m47\punchkid.mstate`, which wrote a rollback
  save state.
- **Correct gfx/music:** none. Current graphics and audio are diagnostic
  first-pass output, not board-authentic M47 parity.
- **Current implementation:** `src/manifests/irem_m47` embeds the local M47
  ROM-set contracts with public `maincpu`, `audiocpu`, `samples`, `gfx8x8`,
  `gfx16x16`, and `proms` region placement, clone-parent inheritance, exact
  local filenames, sizes, and CRC32 values. `src/manifests/irem_m47/m47_system.cpp`
  now assembles a first-pass vertical M47 board with main Z80, sound Z80, two
  YM2149/AY-compatible SSGs, video/color/sprite/work/sound RAM, input/DIP MMIO,
  scroll registers, flip latch, sound-command latch IRQ/ack, region-backed
  diagnostic 2-plane 8x8 and 16x16 video, board identity, and save/load.
  `src/apps/player/adapters/irem_m47` registers `--system irem_m47` / `m47`,
  supports ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or
  in-archive `game.toml` manifests, clone-parent fallback media resolution,
  resident media validation, rollback-ready save-state, vertical display
  metadata, and capability discovery.
- **Remaining:** replace the diagnostic route with board-authentic M47 memory
  and I/O behavior, video/color PROM behavior, input/DIP parity, AY/sample sound
  timing, and trusted visual/audio parity before calling either game correct.

### M52

- **Techsheet games:** Moon Patrol, Tropical Angel.
- **Mnemos games:** `mpatrol`, `mpatrolw`.
- **Smoke playable:** the local ZIP wrappers under `D:\emu\irem\M52` now match
  checked-in M52 manifests. The parent set loads directly; the Williams clone declares
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
  discrete-analog path beyond the currently modeled AY/MSM port surfaces, clarify
  the Moon Patrol / Tropical Angel board split against primary evidence, and prove
  runtime DIP behavior beyond current manual defaults, raster timing, and trusted
  screenshot/audio parity hashes.

### M57

- **Techsheet games:** Tropical Angel.
- **Mnemos games:** first-pass raw-media manifest/player route for `newtangl`.
- **Smoke playable:** the local `D:\emu\irem\M57\newtangl.zip` route loads
  through `MNEMOS_M57_SET_DIR=D:\emu\irem\M57`; direct player proof wrote a
  nonblank 256x256 screenshot with `--system irem_m57` and a rollback save state
  with alias `--system m57`. `newtangl.7z` remains metadata-only until converted
  or unpacked.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m57` embeds the local
  `newtangl` raw-media contract with exact local filenames, offsets, sizes, and
  CRC32 values. The first-pass board derives a Z80 execution window and
  diagnostic graphics window from the contiguous raw media, exposes
  RAM/input/sound-latch surfaces, nonblank video, beeper-backed audio for
  synthetic programs, board identity save/load, player adapter registration,
  capability discovery, and local corpus smoke.
- **Remaining:** replace the first-pass raw-media/Z80 diagnostic route with the
  authentic M57 board map, video/color behavior, Irem Audio path, inputs/DIPs,
  New Tropical Angel timing, and visual/audio parity before calling M57 correct.

### M58

- **Techsheet games:** 10-Yard Fight.
- **Mnemos games:** first-pass ROM manifests/player route for `10yard`,
  `10yardj`, `vs10yard`, and `vs10yardj`.
- **Smoke playable:** all four canonical local ZIPs. `MNEMOS_M58_SET_DIR=D:\emu\irem\M58`
  data-gates CRC-clean media loading and nonblank player smoke for the parent,
  regional clone, and Vs. split sets. Direct `mnemos_player --system irem_m58`
  and alias `--system m58` smokes also prove screenshot and save-state output.
- **Correct gfx/music:** none. Current graphics and audio are diagnostic
  first-pass output, not board-authentic 10-Yard Fight parity.
- **Current implementation:** `src/manifests/irem_m58` embeds 10-Yard Fight ROM
  contracts with board-evidenced `maincpu`, `soundcpu`, `tiles`, `sprites`, and
  `proms` regions, plus `src/manifests/irem_m58/m58_system.cpp` assembles a
  first-pass M58 board with main Z80, sound MC6803, the `soundcpu` high-ROM
  window at `$8000-$ffff`, real reset-vector fetch from `$fffe/$ffff`,
  first-pass MC6803 direct-page latch/SSG MMIO, input/DIP MMIO, sound-command
  latch/IRQ acknowledgement, two YM2149-compatible SSGs, ROM-backed tile/sprite
  rendering, PROM-derived diagnostic color, scroll/flip state, board identity
  save/load, player adapter registration, capability discovery, and local
  corpus smoke.
- **Remaining:** replace first-pass video/color/priority/radar assumptions with
  board-evidenced 10-Yard Fight behavior, finish exact MC6803 ports/timers and
  Irem Audio timing beyond the current first-pass latch/SSG MMIO, add
  board/manual-backed DIP behavior, and collect pinned visual/audio parity
  hashes before calling M58 correct.

### M62

- **Techsheet games:** Kung-Fu Master / Spartan X, Kid Niki, Lode Runner,
  Lot Lot, Spelunker, Lightning Swords, Youjyuden, The Battle-Road, Horizon.
- **Mnemos games:** explicit `ldrun`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`,
  `lotlot`, and `spelunk2` region contracts plus
  raw-media manifests and first-pass player routes for
  `battroad`, `bkungfu`, `horizon`, `kidniki`, `kungfum`,
  `ldrun`, `ldruna`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`,
  `spelunk2`, `spartanx`, `yanchamr`, and `youjyudn`.
- **Smoke playable:** all sixteen checked-in exact local ZIP set routes load
  CRC-clean through the adapter via `MNEMOS_M62_SET_DIR=D:\emu\irem\M62`;
  direct player proof wrote a nonblank
  256x256 `ldrun` screenshot with `--system irem_m62`, a rollback save state
  with alias `--system m62`, and rendered mixed beeper/SSG/MSM audio extraction.
  Current capability output exposes both `memory.msm5205_0.registers` and
  `memory.msm5205_1.registers` for the parent `ldrun` route.
  `ldrun2` now also has direct `--system irem_m62` proof: a nonblank 256x256
  screenshot and a 24,902-byte save-state after 90 frames.
  `ldrun3` now has matching direct proof: a nonblank 256x256 screenshot and a
  16,127-byte save-state after 90 frames.
  `ldrun3j` now has parent-backed direct proof through `ldrun3`: a nonblank
  256x256 screenshot, a 16,189-byte save-state after 90 frames, a 298,324-byte
  rendered WAV, and capability output exposing both YM2149 and both MSM5205
  register surfaces.
  `ldrun4` now has matching direct proof: a nonblank 256x256 screenshot, a
  20,040-byte save-state after 90 frames, and a rendered WAV from the mixed
  beeper/SSG/MSM path.
  `lotlot` now has matching direct proof: a nonblank 256x256 screenshot, an
  11,282-byte save-state after 90 frames, and a rendered WAV from the mixed
  beeper/SSG/MSM path.
  `spelunk2` now has matching direct proof: a nonblank 256x256 screenshot, a
  15,498-byte save-state after 90 frames, a 298,324-byte rendered WAV, and
  capability output exposing both YM2149 and both MSM5205 register surfaces.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m62` embeds local ROM-set
  contracts generated from the Kung-Fu Master / Spartan X, Kid Niki /
  Yanchamaru, Lode Runner, Lot Lot, Spelunker II, Battle Road, Horizon, and
  Youjyuden artifacts. `ldrun`, `ldrun2`, `ldrun3`, `ldrun3j`,
  `ldrun4`, `lotlot`, and `spelunk2` now map into explicit `maincpu`,
  `soundcpu`, graphics, PROM, and timing regions, including the MC6803 reset
  vector in the `$8000-$ffff` sound-ROM window. The M62 adapter now resolves
  clone-parent sibling ZIPs, direct parent directories, and single-inner parent
  ZIP wrappers so `ldrun3j` can inherit shared `ldrun3` artifacts while keeping
  clone-specific program and graphics dumps CRC-verified. `battroad`,
  `bkungfu`, `horizon`, `kidniki`, `kungfum`, `ldruna`, `spartanx`,
  `yanchamr`, and `youjyudn` stay in `raw_media` staging so CRCs, sizes, and
  set grouping are preserved without asserting unfinished title-specific bus
  placement.
  `src/manifests/irem_m62/m62_system.cpp` runs the Z80 execution window, a
  diagnostic graphics window, RAM/input/sound-latch surfaces, nonblank video,
  beeper-backed compatibility audio for synthetic programs, and a first-pass
  MC6803 direct-page sound latch plus dual-SSG and dual-MSM5205 route for
  regioned Lode Runner/Lot Lot/Spelunker II sets. The player adapter exposes Z80, MC6803, both SSG,
  and both MSM5205 register surfaces through capability discovery and mixes the
  two ADPCM decoder queues into extracted audio.
- **Remaining:** replace the diagnostic video and incomplete audio/controller
  assumptions with exact title bus maps, MC6803 port/timer behavior, dual
  MSM5205 stream/control timing, KNA custom video behavior, inputs/DIPs, and
  visual/audio parity before calling any M62 set correct.

### M63

- **Techsheet games:** Wily Tower, Fighting Basketball.
- **Mnemos games:** first-pass ROM manifest and player route for `wilytowr`.
- **Smoke playable:** the local `wilytowr.zip` route loads through
  `MNEMOS_M63_SET_DIR=D:\emu\irem\M63`; direct player proof wrote a nonblank
  256x256 screenshot with `--system irem_m63` and a rollback save state with
  alias `--system m63`.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_m63` embeds the local
  `wilytowr` ROM-set contract with public M63 region placement for `maincpu`,
  `soundcpu`, `gfx1`, `gfx2`, `gfx3`, `user1`, and `proms`. The first-pass
  board assembles a Z80 execution window, RAM/input/sound-latch surfaces,
  synthetic combined graphics media from the declared graphics/PROM regions,
  diagnostic nonblank video, beeper-backed audio for synthetic programs,
  board-identity save/load, player adapter registration, capability discovery,
  and local corpus smoke. The inventory records `wilytowr.zip` as
  `direct_player_loadable` while the two `.7z` Wily Tower archives remain
  `metadata_only_unpack_or_repack`.
- **Remaining:** replace the first-pass diagnostic route with the authentic Z80
  main board, 8039-class sound CPU, AY/sample/discrete sound path,
  tile/sprite/video/color-PROM behavior, inputs/DIPs, Fighting Basketball
  coverage, and visual/audio parity before calling Wily Tower correct.

### Traverse USA / Zippy Race

- **Techsheet games:** not listed as a numbered board row in the current
  factsheet; the local corpus maps this family to public `irem/travrusa.cpp`
  metadata.
- **Mnemos games:** first-pass ROM manifests and player route for `travrusa`,
  `motorace`, `travrusab`, and `travrusab2`.
- **Smoke playable:** four local ROM ZIP routes under `D:\emu\irem\travrusa` are
  data-gated through `MNEMOS_TRAVRUSA_SET_DIR`. Direct player proof includes
  `mnemos_player --system irem_travrusa --rom "D:\emu\irem\travrusa\travrusa (1).zip"
  --frames 60 --screenshot build\scratch\travrusa_parent.ppm`, which wrote a
  240x256 nonblank PPM, and `mnemos_player --system travrusa --rom
  "D:\emu\irem\travrusa\travrusab.zip" --frames 60 --save-state
  build\scratch\travrusab.mstate`, which wrote a save state. The large
  unsuffixed `travrusa.zip` in that folder is artwork/layout, not the parent ROM
  dump; it is metadata-only for support accounting, and the CRC-clean parent ROM
  proof comes from the copy-suffixed ZIP.
- **Correct gfx/music:** none.
- **Current implementation:** `src/manifests/irem_travrusa` preserves the
  `maincpu`, `soundcpu`, `tiles`, `sprites`, and `proms` region placement and
  now assembles a Z80 main CPU, Z80 sound CPU, dual SSG, MSM5205, RAM/MMIO,
  tile/sprite/PROM compositor, inputs, save-state identity, player adapter, and
  clone-parent ZIP fallback. `motorace`, `travrusab`, and `travrusab2` declare
  `travrusa` as the parent and use aliases for shared parent dumps where local
  wrappers only carry unique files.
- **Remaining:** replace first-pass approximations with authentic MotoRace
  encrypted ROM handling, exact Irem Audio timing, title memory/I/O details,
  video priority/scroll/color behavior, DIP/input parity, and visual/audio
  parity.

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
  `D:\emu\irem\M72\rtype.zip`,
  `D:\emu\irem\M72\gallopm72.zip;D:\emu\irem\M72\gallop`,
  `D:\emu\irem\M72\dbreedm72.zip;D:\emu\irem\M81\dbreed.zip`,
  `D:\emu\irem\M72\nspirit.zip`, and
  `D:\emu\irem\M72\airduelm72.zip;D:\emu\irem\M72\airduelm72`.
  The per-set data-gated hooks and M72 corpus smoke runner now accept platform
  path-lists so split sets can provide supplemental media without relying only
  on recursive roster discovery.
  `GLD-M72-ROSTER` now passes with `MNEMOS_M72_SET_DIR=D:\emu\irem\M72` using
  its default 900-frame roster window.
  `GLD-M72-PARITY-HASH` now exists as an opt-in final-frame/audio SHA-256
  ratchet and remains skipped until trusted reference hashes are supplied.
- **Dumped-MCU protected proof:** `MNEMOS_M72_PROTECTED_MCU_SET` points at a
  protected true-M72 set that must contain a real MCU dump; the local
  `D:\emu\irem\M72\nspirit.zip` route passes this golden and proves the adapter
  schedules the MCS-51 instead of falling back to no-dump HLE.
- **Rendered-audio smoke proof:** the M72 corpus smoke runner now has an opt-in
  `-RequireRenderedAudio` gate that runs `mnemos_player --extract-audio` and
  requires the exported rendered WAVE payload to contain nonzero PCM.
  `MNEMOS_M72_PROTECTED_AUDIO_SET` also accepts a path-list such as
  `D:\emu\irem\M72\dbreedm72.zip;D:\emu\irem\M81\dbreed.zip` for the dedicated
  adapter-level CTest golden that steps the protected no-dump-HLE route for 120
  frames and requires nonzero rendered PCM. This proves rendered audio during
  the smoke window, but is still weaker than music/audio parity certification.
  `MNEMOS_M72_PARITY_SET` can now pair trusted source path(s) with
  `MNEMOS_M72_PARITY_FRAME_SHA256` and/or `MNEMOS_M72_PARITY_AUDIO_SHA256` to
  turn that parity evidence into a deterministic CTest assertion.
- **Sample cursor proof:** the sound-Z80 sample-read port and protection-MCU
  MOVX sample-data port now use direct bounded reads against the loaded sample
  region. If either cursor runs past the loaded sample ROM, the read returns
  open bus (`0xff`) rather than wrapping to offset zero.
- **Sample pump proof:** the M72 player schedule now includes the board's
  external sample pump at the documented 32 MHz / 4096 cadence. The pump shares
  the bounded sample cursor, skips zero bytes, writes nonzero bytes to the DAC
  on the sound-clock event timeline, and is exposed as `m72_sample_pump`.
- **No-dump trigger proof:** the `dbreedm72` and `dkgensanm72` HLE sample-trigger
  tables are mechanically checked end-to-end: nine Dragon Breed trigger/start
  pairs and 28 Daiku no Gensan trigger/start pairs.
- **Current local artifact proof:** no checked-in M72 manifest artifact is
  currently missing from the board-local corpus: the recursive preflight against
  `D:\emu\irem\M72` reports `417/417` present. The optional full-roster CTest is
  now current proof: with `MNEMOS_M72_SET_DIR=D:\emu\irem\M72`, it passes with
  its default 900-frame window through the source-ranking and supplemental-media
  routes for every checked-in true-M72 manifest. The stale unpacked
  `D:\emu\irem\M72\nspirit` folder is incomplete, but the current
  `D:\emu\irem\M72\nspirit.zip` is CRC-complete for both `nspirit` and
  `nspiritj`; the corpus smoke runner now ranks that ZIP ahead of the stale
  same-name folder, preserves manifest-named top-level folders inside an
  exact-stem ZIP, and keeps the parent source when subsetting clone media, so
  the same archive can smoke both routes. The board-local recursive artifact
  proof also finds the unpacked `D:\emu\irem\M72\gallop` parent/share folder
  used by `gallopm72`; the sorted `D:\emu\irem\M84\gallop.zip` remains the M84
  Gallop/Cosmic Cop set, not the true-M72 parent source.
- **Current exact local path evidence:** direct CRC scanning of
  `D:\emu\irem\M72\gallopm72.zip`, the unpacked
  `D:\emu\irem\M72\gallop` parent/share folder, and
  `D:\emu\irem\M72\nspirit.zip` now reports all focused artifacts present for
  `gallopm72` plus World/Japan `nspirit`. Direct ZIP inspection confirms
  `D:\emu\irem\M72\gallopm72.zip` contains `cc_c-pr-.ic1` CRC `0xac4421b1`,
  while `D:\emu\irem\M72\nspirit.zip` contains `nin_c-pr-b.ic1` CRC
  `0x0f7b2713` and `nspiritj/nin_c-pr-.ic1` CRC `0x802d440a`; the scanner
  reports `48/48` present when checking `nspirit` and `nspiritj` from that ZIP.
  Exact scans of
  `D:\emu\irem\M72\lohtj.zip` plus `D:\emu\irem\M72\loht.zip` report `20/20`
  for `lohtj`, and `D:\emu\irem\M72\lohtb2.zip` plus `D:\emu\irem\M72\loht.zip`
  report `30/30` for `lohtb2`; the current exact `.7z` check of
  `D:\emu\irem\M72\lohtj.7z`, `D:\emu\irem\M72\lohtb2.7z`, and the parent
  `loht.zip` reports `50/50`. The player manifest now aliases the local
  `loht.zip` parent/shared filenames used by those clone routes, and targeted
  `gallopm72` / `lohtj` / `lohtb2` corpus smoke passes `3/3`.
- **Current mixed-archive scan evidence:** the M72 artifact scanner now skips
  unreadable entries inside unrelated `.7z` archives and malformed ZIPs instead
  of aborting the corpus walk. The previously failing
  `D:\emu\Chaos Field (English v1.0)[Analog Stick Enabled][cdi].7z` probe now
  completes as a 0/20 non-match, and a rerun across `D:\emu\irem` plus
  `D:\emu\Darksoft Apocalypse M72 2020-12-30.7z` previously reported partial
  blocker counts, but those are superseded by the updated M72 ZIPs. Current live
  proof against `D:\emu\irem\M72` with `-Recurse` reports `118/118` for the
  prior `gallopm72`/`nspirit`/`nspiritj`/`lohtj`/`lohtb2` blocker group and
  `417/417` for the checked-in M72 manifest preflight.
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
- **Mnemos games:** `cosmccop`, `dkgensan`, `dkgensana`, `gallop`, `hharryb`,
  `hharryu`, `kengo`, `kengoj`, `ltswords`.
- **Smoke playable:** all nine checked-in M84 sets load through `--system
  irem_m84`. The Hammerin' Harry / Daiku no Gensan split sets compose with the
  M81 `hharry` parent media, with the M84 program second pair placed at
  `0x60000` and reloaded at `0xe0000`. `kengo` and `kengoj` compose with the
  local `ltswords` parent, select the V35 profile, and inherit the
  Lightning Swords graphics/sound/sample contract. Gallop/Cosmic Cop media load
  with CRC-verified program, sound, graphics, samples, PROMs, and PLDs; the
  Gallop manifest carries 10 DIP switch definitions, and the adapter applies
  their composed default (`0xf9bf`) to the board DIP register for both Gallop
  and Cosmic Cop.
- **Correct gfx/music:** not certified.
- **Local corpus note:** the organized corpus now stores the primary M84 routes
  under `D:\emu\irem\M84`, while the inventory still classifies historical
  cross-board archive matches by manifest rather than folder name. The current
  `ltswords` route lacks the small PROM/PLD artifacts listed for the complete
  board; that explicit `irem_m84_prom_pld` HLE declaration is inherited by
  `kengo` and `kengoj`. The Daiku no Gensan ZIPs use the M81 `hharry` parent
  for shared graphics and CRC-identical PROM/PLD aliases while keeping their
  own M84 program, sound, sample, and available PAL dumps.
- **Remaining:** replace or verify the M81-compatible wrapper assumptions with
  board evidence, resolve the R-Type II classification mismatch, recover/prove
  the `ltswords`/Ken-Go PROM/PLD artifacts, and prove M84-specific memory/I/O,
  priority, raster timing, DIP behavior beyond current manifest defaults, and
  screenshot/audio parity.

### M85

- **Techsheet games:** Pound for Pound.
- **Mnemos games:** `poundfor`, `poundforj`.
- **Smoke playable:** `D:\emu\irem\M85\poundfor.zip` and
  `D:\emu\irem\M85\poundforj.zip` load CRC-clean through
  `--system irem_m85` / `m85`; `poundforj` inherits parent media from
  `poundfor`. Focused data-gated tests and direct player smoke wrote 384x256
  nonblank PPM screenshots plus save-state bytes after 90 frames.
- **Correct gfx/music:** not certified. Current rendering/audio are diagnostic
  first-pass output through a shared M81-compatible V30/Z80/YM2151/DAC core.
- **Remaining:** replace or verify the compatibility-core assumptions with M85
  board evidence, prove memory/I/O behavior, Pound for Pound video/priority,
  inputs and DIP behavior, raster timing, audio timing, and visual/audio parity.

### M90 / M97 / M99

- **Techsheet games:** Bomber Man / Bomber Man World / Dyna Blaster / Atomic
  Punk, Hasamu, Quiz F-1.
- **Mnemos games:** `atompunk`, `bbmanw`, `bbmanwj`, `bbmanwja`, `gussun`,
  `hasamu`, `newapunk`, `quizf1`, `riskchal`.
- **Smoke playable:** all nine current local M90 ZIPs under `D:\emu\irem\M90`
  load CRC-clean through `--system irem_m90`/the M90 adapter data gate, step one
  frame, produce a 384x256 nonblank diagnostic frame, and produce save-state
  bytes. The adapter resolves split-clone shared media from `bbmanw.zip` for
  Atomic Punk/Bomber Man World variants and from `riskchal.zip` for `gussun`.
  The adapter also maps P1/P2 service plus operator-test
  inputs to the board-visible active-low system port, preserves them across
  save/load, retains parsed manifest DIP metadata, folds parsed DIP defaults
  into the 16-bit board DIP register, exposes `DIP switches` when manifests
  provide switch tables, and still honors explicit `--dip` overrides.
- **Correct gfx/music:** not certified. The board shell now matches the
  V35/Z80/YM2151/DAC topology, but rendering is a GA25 diagnostic compositor and
  audio proof is limited to the Z80/YM/DAC route plus sound-Z80-clocked
  synthetic DAC mixing.
- **Local corpus note:** the M90 corpus is now organized under
  `D:\emu\irem\M90`. `quizf1` records its extra banked V35 program pair as
  resident media, but the current board shell does not yet map it
  authentically.
- **Remaining:** authentic GA25 tile/sprite/row-scroll behavior, V35 on-die
  interrupt/timer behavior and Quiz F-1 bank switching, board-authentic DIP
  tables/runtime behavior, and screenshot/audio parity.

### M92

- **Techsheet games:** Gunforce, Lethal Thunder / Thunder Blaster, R-Type Leo,
  In the Hunt, Undercover Cops,
  Ninja Baseball Bat Man, Blade Master, Mystic Riders, Major Title 2, Hook,
  Superior/Perfect Soldiers, Gunforce 2.
- **Mnemos games:** `bmaster`, `crossbld`, `geostorm`, `gunforce`,
  `gunforcej`, `gunforceu`, `gunforc2`, `gunhohki`, `hook`, `inthunt`,
  `inthuntu`, `lethalth`, `mysticri`, `mysticrib`, `nbbatman`, `nbbatmanu`,
  `rtypeleo`, `rtypeleoj`, `thndblst`, `uccops`, `uccopsar`, `uccopsj`, and
  `uccopsu`
  as checked-in manifests under `irem_m92`.
- **Smoke playable:** all twenty-three checked-in M92 manifests load through
  `--system irem_m92`/the M92 adapter data gate, step one frame, produce a 320x240
  nonblank diagnostic frame, and produce save-state bytes. `crossbld`,
  `geostorm`, `inthuntu`, `gunforcej`, `gunforceu`, `mysticri`, `gunhohki`,
  `mysticrib`, `nbbatman`, and `nbbatmanu` also have direct `mnemos_player`
  screenshot/save-state smokes. `rtypeleo.zip` and split-clone
  `rtypeleoj.zip` now have direct `mnemos_player --system irem_m92` proof:
  320x240 nonblank PPMs after 60 frames and save-state containers.
  `uccops.zip` and split-clone `uccopsu.zip` have direct nonblank PPM proof;
  `uccopsj.zip` and `uccopsar.zip` have direct save-state proof.
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
- **Local corpus note:** the current `D:\emu\irem\M92` bucket has 56 tracked
  M92 manifest-name matches, 24 direct player-loadable/supported ZIP routes,
  and 32 metadata-only routes, including `.7z` archives and artwork/layout
  packages. Twenty-three checked-in M92-era set IDs now resolve to embedded
  manifests and load CRC-clean through `MNEMOS_M92_SET_DIR`: Blade Master
  parent/Japan clone (`bmaster`, `crossbld`), Gunforce parent
  (`gunforce`), Gunforce Japan/US split clones (`gunforcej`, `gunforceu`) via
  parent fallback, Gunforce 2 / Geostorm (`gunforc2`, `geostorm`), Hook
  (`hook`), In the Hunt parent/US clone (`inthunt`, `inthuntu`), Lethal Thunder
  parent/Japan clone (`lethalth`, `thndblst`) via parent fallback, Mystic Riders
  (`mysticri`), Gun Hohki (`gunhohki`), the Mystic Riders split bootleg route
  (`mysticrib`), Ninja Baseball Bat Man (`nbbatman`), the US split clone
  (`nbbatmanu`) via parent fallback, R-Type Leo parent/Japan clone
  (`rtypeleo`, `rtypeleoj`) via parent fallback, and Undercover Cops
  parent/US/Japan/Alpha Renewal (`uccops`, `uccopsu`, `uccopsj`, `uccopsar`) via
  parent fallback. `rtypeleo (1).zip` is an artwork/layout package and is
  tracked as metadata-only, not as ROM proof.
- **Remaining:** derive/verify the proprietary M92 V35 decrypt transform/key,
  cycle-exact V35 interrupt latency, exact GA20/YM2151 sound protocol,
  GA21/GA22 video/priority behavior, exact memory/I/O maps, protection details,
  DIP/raster behavior, and visual/audio parity proof.

### M107

- **Techsheet games:** Fire Barrel, Dream Soccer '94, World PK Soccer.
- **Mnemos games:** `airass`, `dsoccr94`, `firebarr`.
- **Smoke playable:** all three checked-in sets are data-gated through
  `MNEMOS_M107_SET_DIR`; the current M107 bucket has 8 tracked local artifacts,
  5 direct player-loadable/supported routes, and 3 metadata-only `.7z` routes.
  Air Assault has direct nonblank screenshot and save/load smoke, and Dream
  Soccer '94 now has direct `mnemos_player --system irem_m107 --rom
  "D:\emu\irem\M107\dsoccr94.zip"` proof: 384x256 nonblank PPM after 60 frames
  and a save-state container. Fire Barrel is CRC-clean and player-routable, but
  not parity-certified. `airass` and `firebarr` carry the shared Fire Barrel /
  Air Assault SW1/SW2 and SW3 DIP profile; `dsoccr94` carries its four-player
  Time, Difficulty, Game Mode, Starting Button, Cabinet, coinage, and SW3 Player
  Power profile. The adapter applies the composed SW1/SW2 default (`0xffbf`),
  applies the separate SW3 `COINS_DSW3` default (`0xebff` for `airass` /
  `firebarr`, `0xffff` for `dsoccr94`), exposes per-set DIP counts in system
  spec, and maps service/test frontend inputs to the M107 `COINS_DSW3`
  service-credit and operator-service bits.
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
2. Advance `irem_travrusa` from first-pass smoke-playable to authentic behavior:
   MotoRace still needs encrypted-ROM handling, the video/audio path needs board
   parity proof, and the unsuffixed `travrusa.zip` remains artwork/layout rather
   than ROM evidence.
3. Advance M52 Moon Patrol from first-pass route to authentic video/audio by
   replacing the diagnostic compositor and first-pass digital audio with board-evidenced
   parallax, road, sprite, sound-CPU-owned MSM5205 stream timing, and discrete
   analog behavior.
4. Resolve the M82/M84 R-Type II classification mismatch with board evidence and
   adjust manifests/docs if needed.
5. Use the now-passing M72 roster golden as the baseline for the next
   protection/DIP/parity slices; the remaining M72 work is not missing media but
   stronger authenticity proof.
6. Advance M63 Wily Tower from first-pass smoke-playable to authentic behavior:
    the Z80/8039 board map, AY/sample/discrete audio, video/color PROM behavior,
    inputs/DIPs, Fighting Basketball coverage, and visual/audio parity remain
    open.
7. Advance M57 New Tropical Angel from first-pass smoke-playable to authentic
   behavior: the real M57 memory/I/O map, video/color behavior, Irem Audio path,
   inputs/DIPs, timing, and visual/audio parity remain open.
8. Advance M14 P.T. Reach Mahjong from first-pass smoke-playable to authentic
   behavior by replacing the 8080-compatible surrogate with board-evidenced
   8085 timing, input, color, and sound behavior.
9. Advance M47 Oli-Boo-Chu / Punching Kid from first-pass smoke-playable to
   authentic behavior: memory/I/O timing, video/color PROM behavior, AY/sample
   sound timing, input/DIP parity, and visual/audio parity remain open.
9. Promote M62 from the first-pass Lode Runner-family MC6803/dual-SSG/dual-MSM5205
   slice and raw-media fallback route to an authentic Z80/M6803/KNA/MSM5205 board
   profile before calling any Lode Runner, Spelunker II, Battle Road, or Youjyuden
   set correct.
10. Use `scripts\irem\run-local-corpus.ps1 -IncludeFullM72Roster` for the strict
   M72 roster proof. With the switch, the runner prints a checked-in-manifest
   artifact preflight before CTest; without the switch it is the available-artifact proof
   runner for every implemented Irem family.
11. Advance M90 from a diagnostic V35/Z80/YM/DAC shell to authentic GA25 video
   once complete graphics media and board evidence are available.
12. Advance the M92 first-pass profile from diagnostic execution to authenticity
   by resolving encrypted V35 sound-CPU behavior and GA21/GA22 video evidence.
