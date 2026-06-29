# Irem Arcade System Boards — Hardware Reference

Chronological catalogue of Irem's "M-series" arcade system boards, oldest to
newest, expanded with CPU, sound, custom-silicon, and representative titles.

**How to read the year ranges:** dates are *usage spans* (first-to-last shipping
title on the hardware), not single launch dates. This is why the V30-era custom
family (M72/M81/M82/M84/M85, plus adjacent M75-era hardware) overlaps heavily -
they were concurrent sister boards, not sequential generations.

**Confidence note:** the high-volume boards (M62, M72, M81/82/84/85, M90, M92,
M107) are well-documented and cross-verified. The sparse boards (M14, M27, M47,
M57, M63, M77, M97, M99, M119) have thin public documentation; entries flag what
is uncertain rather than assert it.

---

## Master chronology

| Board | Era | Architecture family | Anchor title |
|-------|-----|---------------------|--------------|
| M10 / M15 | ~1978–1980 | Intel 8085A, discrete | IPM Invader |
| M14 | 1979 | NEC D8085AC / Intel 8085A, discrete | P.T. Reach Mahjong |
| M27 | 1980 | M6502, early 8-bit isolated | Panther |
| M47 | 1981 | Z80 + Z80, early 8-bit isolated | Oli-Boo-Chu |
| M52 | 1982 | Z80, 8-bit | Moon Patrol |
| M57 | 1982–1983 | Z80, 8-bit | Tropical Angel |
| M58 | 1983 | Z80, 8-bit | 10-Yard Fight |
| M62 | 1984–1988 | Z80, 8-bit (large family) | Kung-Fu Master |
| M63 | 1984–1985 | Z80, 8-bit | Wily Tower |
| M72 | 1987–1991 | NEC V30 | R-Type |
| M75 | 1988 | Z80 + Z80, YM2151/DAC (Vigilante route) | Vigilante |
| M77 | 1988 | (sparse) | — |
| M81 | 1989–1990 | NEC V30 | Hammerin' Harry, Dragon Breed |
| M82 | 1989–1990 | NEC V30 | Major Title |
| M84 | 1989–1991 | NEC V30 / V35 | R-Type II, Cosmic Cop |
| M85 | 1990 | NEC V30 | Pound for Pound |
| M90 | 1991–1992 | NEC V35 (single board) | Bomber Man / Dyna Blaster |
| M97 / M99 | 1992–1993 | NEC V35 (M90 variants) | (sparse) |
| M92 | 1991–1994 | NEC V33 + V35 | Gunforce, Lethal Thunder / Thunder Blaster, In the Hunt, R-Type Leo |
| M107 | 1993–1995 | NEC V33 + V35 | Fire Barrel, Dream Soccer '94 |
| M119 | ~2000 | HD6417708S / SH-3-class, sparse isolated board | Slotters Club: Umi Monogatari |

---

## Era 1 — Discrete & early 8-bit (1978–1985)

### M10 / M15
- **Years:** ~1978–1980
- **Main CPU:** Intel 8085A
- **Sound:** discrete analog circuitry plus simple samples; no programmable
  PSG in the modern sense
- **Notable titles:** IPM Invader, Space Beam, Sky Chuter, Head On, Green Beret,
  Andromeda
- **Notes:** Irem's first-generation boards, from the IPM/early-Irem era. A
  separate lineage from everything that follows — fixed/tile graphics, no
  microprocessor-driven sound subsystem. The M10 and M15 are board variants of
  the same generation.

### M14
- **Years:** 1979
- **Main CPU:** NEC D8085AC / Intel 8085A lineage
- **Sound:** sparse public evidence; sample/discrete sound is still treated as
  unverified
- **Notable titles:** P.T. Reach Mahjong
- **Notes:** Early isolated Irem board. Current public driver-level evidence
  names M14 hardware and gives a small 8085 program/graphics ROM map, but color,
  sound, timing, and input behavior need board-specific proof before any
  executable profile is treated as authentic.

### M27
- **Years:** 1980
- **Main CPU:** MOS Technology M6502 in current public driver-level evidence
- **Sound:** Panther-specific audio board with a small audio CPU ROM in current
  driver-level metadata; exact audio behavior is still unverified in Mnemos
- **Notable titles:** Panther
- **Notes:** Early isolated Irem board grouped with the public Red Alert-family
  driver metadata but marked as Irem M27 hardware. Treat it as a ROM-contract
  and corpus-grouping target until board photos, schematics, or manual data
  prove exact video, color, sound, and input behavior.

### M47
- **Years:** 1981
- **Main CPU:** Zilog Z80
- **Sound:** Zilog Z80 sound CPU with AY-3-8910 plus sample/CVSD-style path in
  current driver-level evidence
- **Notable titles:** Oli-Boo-Chu, Punching Kid
- **Notes:** Public driver evidence labels this as "Irem M47 hardware?" from PCB
  markings and explicitly separates it from the later M52 lineage. Treat M47 as
  an isolated early-8-bit board until board photos, schematics, or manual data
  prove deeper relationships.

### M52
- **Years:** 1982
- **Main CPU:** Zilog Z80 @ 3 MHz
- **Sound:** "Irem Audio" subsystem — AY-3-8910 PSG(s) with MSM5205 ADPCM and
  discrete analog stages (the arrangement that standardizes on M62)
- **Notable titles:** Moon Patrol
- **Notes:** First of the scrolling-background 8-bit boards. Moon Patrol is the
  landmark title (parallax scrolling, licensed to Williams in North America).
  Tropical Angel uses related M52-era audio hardware, but the current local ROM
  contract is tracked under the M57 video/system route rather than the Moon
  Patrol M52 board profile.

### M57
- **Years:** 1982–1983
- **Main CPU:** Zilog Z80
- **Sound:** Irem Audio (AY-3-8910 + MSM5205 lineage)
- **Notable titles:** Tropical Angel, New Tropical Angel
- **Notes:** Sparse documentation; closely related to the M52/M58 generation.

### M58
- **Years:** 1983
- **Main CPU:** Zilog Z80
- **Sound:** Irem Audio (AY-3-8910 + MSM5205 lineage)
- **Notable titles:** 10-Yard Fight
- **Notes:** Same early-8-bit lineage; precursor to the much larger M62.

### M62
- **Years:** 1984–1988 (the largest 8-bit family by title count)
- **Main CPU:** Zilog Z80 @ 3.072 MHz
- **Sound CPU:** Motorola M6803 @ ~0.894 MHz
- **Sound chips:** 2× AY-3-8910 PSG + 2× OKI MSM5205 ADPCM, plus discrete/netlist
  analog
- **Key customs:** KNA6034201 (shared with M72), KNA6032701 (shared with
  M72/M75)
- **Notable titles:** Kung-Fu Master / Spartan X (1984), Kid Niki, Lode Runner,
  Lot Lot, Spelunker, Lightning Swords, Youjyuden, The Battle-Road
- **Notes:** Genre-defining hardware — Kung-Fu Master is widely cited as the first
  beat 'em up. The sound topology (M6803 + dual AY-3-8910 + dual MSM5205) is the
  mature form of the "Irem Audio" board.

### M63
- **Years:** 1984–1985
- **Main CPU:** Zilog Z80
- **Sound:** Intel 8039-class sound CPU with AY-3-8910 / sample-discrete path
  evidence on Wily Tower
- **Notable titles:** Wily Tower, Fighting Basketball
- **Notes:** Minor late-8-bit board; sparse documentation.

---

## Era 2 — Late-1980s custom "M72 family" (1987–1991)

Most boards in this family share a common architectural core:
**NEC V30 main CPU**, **Z80 @ 3.579545 MHz sound CPU**, **Yamaha YM2151 (OPM) FM
@ 3.579545 MHz**. M75/Vigilante is the important local exception: current
reference-source evidence routes it as a **Z80 main CPU plus Z80 sound CPU** with
the same YM2151/DAC audio generation. These boards still overlap in customs and
era, but CPU family alone is not sufficient board proof.

**Shared NANAO custom silicon across the family** (M72/M75/M81/M82/M84/M85):
- `KNA91H014` — palette RAM
- `KNA70H015` (11) — video counter
- `KNA70H016` (12) — DMA controller / sprite
- `KNA71H009` (13), `KNA72H010` (14) — sprites
- `KNA71H010` (15) — background tiles
- `KNA65005-17` — sprite-related

### M72
- **Years:** 1987–1991
- **Main CPU:** NEC V30 @ 8 MHz
- **Sound CPU:** Z80 @ 3.579545 MHz
- **Sound chip:** YM2151 @ 3.579545 MHz
- **Topology:** 3-PCB stack; JAMMA edge on the centre board
- **Notable titles:** R-Type, Ninja Spirit, Image Fight, Legend of Hero Tonma,
  Dragon Breed, X-Multiply, Air Duel, Daiku no Gensan
- **Notes:** The flagship of the family and the most-converted target — nearly all
  M8x games run (or have been made to run) on M72.

### M75
- **Years:** 1988
- **Main CPU:** Zilog Z80 @ 3.579545 MHz for the current Vigilante route.
- **Sound CPU:** Z80 @ 3.579545 MHz
- **Sound chip:** YM2151 @ 3.579545 MHz plus 8-bit DAC/sample path
- **Notable titles:** Vigilante, including official regional revisions now
  tracked as `vigilant`, `vigilanta`, `vigilantb`, `vigilantc`, `vigilantd`,
  `vigilantg`, and `vigilanto`.
- **Notes:** Closely related in era/custom usage to the M72 family, but the
  executable Vigilante path is not a V30 board. Keep M75 separated from M72/M81
  routing unless new board evidence proves a different title-specific variant.
- **Current Mnemos hardware hooks:** two-bank KNA91-style 5-bit palette RAM is
  modeled for CPU-visible writes/readback, and the Vigilante rear-color register
  preserves the rear-layer disable bit separately from the masked color code.
  Official split clone wrappers resolve their shared media through the complete
  local `vigilant` parent; bootleg coverage remains separate.

### M77
- **Years:** 1988
- **Notes:** Very sparsely documented; listed in Irem's board sequence but with
  little public hardware detail. No confident CPU/sound/title attribution.

### M81
- **Years:** 1989–1990
- **Main CPU:** NEC V30
- **Sound CPU:** Z80 @ 3.579545 MHz
- **Sound chip:** YM2151
- **Topology:** 2-board stack, both boards same size, JAMMA edge on upper board,
  no inter-board cables; 3 PCB connectors (distinguishes it from M82)
- **Notable titles:** Hammerin' Harry (World), Dragon Breed, X-Multiply
- **Notes:** Visually near-identical to M82; the connector layout is the tell.

### M82
- **Years:** 1989–1990
- **Main CPU:** NEC V30
- **Sound CPU:** Z80 @ 3.579545 MHz
- **Sound chip:** YM2151
- **Topology:** 2-board stack; **doubled sprite circuitry**, an extra sprite
  layer, larger tilemap, and row-scroll/row-select support
- **Notable titles:** Major Title (the only title that actually uses the extra
  sprite layer), plus M82 builds of Air Duel and Daiku no Gensan
- **Notes:** The most capable graphics variant in the family, but its extra
  capability went almost entirely unused.

### M84
- **Years:** 1989–1991
- **Main CPU:** NEC V30 — **but Cosmic Cop and Ken-Go use a NEC V35** with an
  embedded interrupt controller instead
- **Sound CPU:** Z80 @ 3.579545 MHz
- **Sound chip:** YM2151
- **Topology:** 2-board stack, JAMMA edge on lower board, upper board half-size;
  two motherboard and two ROM-board variants exist
- **Notable titles:** R-Type II, Hammerin' Harry (US) / Daiku no Gensan,
  Cosmic Cop, Ken-Go
- **Notes:** The V30-vs-V35 split makes M84 the least convertible board in the
  family — a driver author has to branch on CPU per title.

### M85
- **Years:** 1990
- **Main CPU:** NEC V30
- **Sound CPU:** Z80 @ 3.579545 MHz
- **Sound chip:** YM2151
- **Topology:** like M84 but PCB connectors on either side rather than the back
  edge; the cheapest board of the family
- **Notable titles:** Pound for Pound
- **Notes:** Effectively a cost-reduced M84 variant.

---

## Era 3 — NEC V35 single-board (1991–1993)

### M90 (and variants M97 / M99)
- **Years:** 1991–1992 (M90); M97/M99 ~1992–1993
- **Main CPU:** NEC V35 — a microcontroller derivative of the V30 with a built-in
  interrupt controller (which these games use), plus on-die timers/DMA (unused)
- **Sound CPU:** Z80
- **Sound chip:** YM2151 (audio hardware is nearly identical to M72/M84)
- **Graphics:** single `GA25` custom handling **both** sprites and tilemaps —
  two tilemap layers, up to 84 sprites, with row scroll/row select
- **Notable titles:** Bomber Man / Bomber Man World (a.k.a. Dyna Blaster in
  Europe, Atomic Punk in the US), Hasamu, Quiz F-1
- **Notes:** Architecturally a bridge — V35 main CPU (pointing toward M92) but the
  classic Z80/YM2151 audio of the V30 era. M97 and M99 are documented as the same
  single-board platform; some older sources instead group M97 with the V30 family,
  so treat M97's placement as not fully settled.

---

## Era 4 — NEC V33 16-bit (1991–2000)

### M92
- **Years:** 1991–1994
- **Main CPU:** NEC V33 @ 9 MHz (with a V30 at 7.159 MHz in some configs)
- **Sound CPU:** NEC V35 @ 14.318 MHz — **encrypted** (NANAO 08J27261A1); sound
  code is decrypted by a per-CPU-model key, with jumpers J1/J6 (S→N) to bypass
- **Sound chips:** YM2151 (OPM) @ 3.579545 MHz + **Irem GA20** PCM @ 3.579545 MHz
- **Graphics customs:** NANAO `GA21` & `GA22`
- **Display:** 320×240, up to 32,768 colours (15-bit); up to 4 players, 6 buttons
- **Notable titles:** Gunforce, Lethal Thunder / Thunder Blaster, R-Type Leo,
  In the Hunt, Undercover Cops, Ninja Baseball Bat Man, Blade Master,
  Mystic Riders, Major Title 2, Hook, Superior/Perfect Soldiers, Gunforce 2
- **Notes:** Irem's mature 16-bit platform. The GA20 PCM chip replaces the older
  ADPCM approach and becomes the house sample engine through the end.

### M107
- **Years:** 1993–1995
- **Main CPU:** NEC V33 @ 14 MHz
- **Sound CPU:** NEC V35 @ 14.3 MHz
- **Sound chips:** YM2151 @ 3.5 MHz + Irem GA20 @ 3.5 MHz
- **Notable titles:** Fire Barrel, Dream Soccer '94, World PK Soccer
- **Notes:** Essentially an M92 evolution clocked higher. Dream Soccer '94's
  Japanese revisions actually run on M92 — a useful reminder that the M92/M107
  split is incremental. This is Irem's last arcade hardware generation of the
  video-game era; the company exited arcade game development in 1994.

### M119
- **Years:** ~2000
- **Main CPU:** HD6417708S / SH7708S-class SH-3, 60 MHz in public driver metadata
- **Video:** NEC uPD94244-210 VDP (not yet emulated upstream)
- **Sound:** YMZ280B sample player
- **Notable titles:** Slotters Club: Umi Monogatari (`scumimon`)
- **Notes:** A very late, isolated gambling/slot board entry, well after Irem's
  main arcade video-game run. Current public M119 driver metadata marks the set
  not working and no-sound, so Mnemos treats it as a ROM-contract/classification
  target until SH-3, uPD94244, and YMZ280B support exist.

---

## Architectural through-line (for emulator authors)

Three observations that matter if you're modelling this lineage as CPU-core and
driver families rather than per-game one-offs:

1. **NEC main-CPU progression:** 8085A → Z80 → **V30** (M72 family) → **V35**
   (M90, plus M84's two odd titles) → **V33** (M92/M107). The V30→V35→V33 jumps
   are the natural core boundaries; the V35 is a V30 microcontroller superset, so
   a V30 core with the V35's on-die interrupt controller/timers covers most of the
   middle-to-late catalogue.

2. **Audio continuity:** a **Z80 + YM2151** sound subsystem persists unchanged
   from M72 all the way through M90. M92/M107 swap the Z80 sound CPU for an
   *encrypted* V35 and add the **GA20** PCM chip, but keep the YM2151. So one
   YM2151 model plus one GA20 model covers the entire late catalogue; the 8-bit
   era is the separate AY-3-8910 + MSM5205 world.

3. **NANAO custom-silicon families:** the `KNA…` parts are shared wholesale across
   the V30 family (palette, video counter, DMA, sprites, tiles), then consolidate
   into fewer, larger `GA…` parts (GA20/GA21/GA22/GA25) in the V35/V33 era. Driver
   identity is best keyed on the custom-chip set, not the board number, since board
   numbers blur (M81 vs M82 vs M84) while the silicon is decisive.

---

## Sources

Cross-referenced from: System16 (The Arcade Museum) hardware pages; MAME source
(`src/mame/irem/*.cpp`); Emulation General Wiki; vgmrips hardware pages;
JAMMArcade.net repair logs; the Arcade-Projects "M72/M81/M82/M84/M85" ID thread;
and the MiSTer/OpenGateware FPGA core documentation for M72/M90/M92.
