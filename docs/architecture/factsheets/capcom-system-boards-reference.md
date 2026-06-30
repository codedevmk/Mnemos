# Capcom Arcade System Boards — Hardware Reference

Chronological catalogue of Capcom's arcade system hardware, oldest to newest,
expanded with CPU, sound, custom-silicon, protection scheme, and representative
titles. Companion to the Irem reference; same conventions.

**How to read the year ranges:** dates are *usage spans* (first-to-last shipping
title), not single launch dates. CPS-1 hardware, for example, kept shipping into
2000 even though its design dates to 1988.

**Confidence note:** the CP System generations (CPS-1, Dash, CPS-2, CPS-3) are
exhaustively documented and cross-verified here. The pre-CPS bespoke boards and
the outsourced post-CPS 3D platforms (ZN, NAOMI) are summarised at family level
with confidence flags rather than per-board precision, because pre-CPS Capcom did
not ship a single named reusable "system."

---

## Master chronology

| System | Era | Main CPU | Sound | Anchor titles |
|--------|-----|----------|-------|---------------|
| Pre-CPS (bespoke) | ~1984–1988 | Z80 → 68000 | Z80 + YM2203 / YM2151 (+ MSM5205) | 1942, Ghosts'n Goblins, Commando |
| **CPS-1** | 1988–1995 (hw to 2000) | 68000 @ 10/12 MHz | Z80 + YM2151 + MSM6295 | Final Fight, Street Fighter II |
| **CPS-1.5 / CPS Dash** | 1992–1993 | 68000 @ 10 MHz | Z80 + **QSound** DSP | Cadillacs and Dinosaurs, The Punisher |
| **CPS-2** | 1993–2003 | 68000 @ 16 MHz (encrypted) | Z80 (Kabuki) + QSound | SF Alpha/Zero, Marvel vs Capcom, X-Men |
| **CPS-3** | 1996–1999 | Hitachi SH-2 @ 25 MHz (encrypted) | custom 16-ch sample chip | Street Fighter III, JoJo, Red Earth |
| Capcom ZN-1 / ZN-2 | 1995–1998 | Sony PlayStation-based (R3000) | — (Sony/QSound) | Street Fighter EX, Star Gladiator |
| → Sega NAOMI (adopted) | 1999– | SH-4 (Dreamcast-based) | — | Marvel vs Capcom 2, Power Stone |

---

## Pre-CPS — bespoke per-game boards (~1984–1988)

- **Main CPU:** Zilog Z80 on the early titles, migrating to **Motorola 68000** by
  1987 (Tiger Road, Last Duel, F-1 Dream)
- **Sound CPU:** Z80
- **Sound chips:** YM2203 (OPN) and/or YM2151 (OPM), frequently paired with OKI
  MSM5205 ADPCM for samples
- **Notable titles:** 1942 (1984), Commando (1985), Ghosts'n Goblins (1985),
  Section Z, Gun.Smoke, Trojan, SonSon, Exed Exes, 1943, Side Arms, Black Tiger,
  Bionic Commando, Tiger Road, Last Duel
- **Notes:** Before CPS, Capcom built closely-related but per-game board sets
  rather than one reusable platform — the cost problem CPS-1 was created to solve.
  Capcom had already established hit franchises (Ghosts'n Goblins) on this
  hardware. Treat exact per-title chip configs as needing verification; the
  Z80→68000 main-CPU transition and the YM2203/YM2151 + MSM5205 audio lineage are
  the reliable through-line.

---

## CPS-1 — CP System (1988)

- **Main CPU:** Motorola 68000 (CMOS) @ 10 MHz; bumped to **12 MHz** from
  Street Fighter II: Champion Edition onward (~20% system speed-up)
- **Sound CPU:** Zilog Z80 @ 3.579545 MHz, with 2 KB work RAM
- **Sound chips:** Yamaha **YM2151** (8-channel FM) @ 3.579545 MHz +
  OKI **MSM6295** (4-channel ADPCM, ~4–32 kHz) @ 7.576 MHz
- **Graphics:** Capcom **CPS-A** + **CPS-B** custom ASICs ("CPS Super Chips")
  @ 16 MHz — the pair cost ~$10M to develop and did the work of ~10 discrete
  boards of the era
- **Memory:** 64 KB main work RAM, 192 KB video RAM, Z80 2 KB
- **Display:** 384×224 active (512×256 overscan), ~59.6 Hz; 12-bit RGB with a
  4-bit brightness value (4,096 on-screen from a 65,536 palette)
- **Board structure:** A-board (fixed: CPU/sound) + B-board (swappable game ROMs,
  carries the CPS-B config) + later **C-board** (I/O, including the 6-button
  extension from Street Fighter II)
- **Protection:** on later games the CPS-B layer/priority registers are held in
  battery-backed RAM (an early form of the "suicide" approach perfected on CPS-2)
- **Notable titles:** Forgotten Worlds (1988), Ghouls'n Ghosts, Strider,
  Dynasty Wars, Final Fight, Captain Commando, The King of Dragons, Knights of
  the Round, Carrier Air Wing, Mega Man: The Power Battle, **Street Fighter II /
  Champion Edition / Hyper Fighting**
- **Dev note:** Capcom used the **Sharp X68000** computer as the CPS-1 design
  benchmark and game-development workstation — which is why several CPS-1 titles
  got near-pixel-perfect X68000 ports.

---

## CPS-1.5 / CP System Dash (1992–1993)

- A cost-reduced CPS-1 in an enclosed grey case, whose defining change is the
  audio path.
- **Main CPU:** 68000 @ 10 MHz (program code **not** encrypted)
- **Sound CPU:** Z80 @ 6 MHz, with sound ROMs encrypted by a **"Kabuki" Z80**
  (VLSI custom)
- **Sound chip:** **QSound** DSP (DL-1425) @ 4 MHz on a dedicated Q-board —
  positional/3D audio, replacing the YM2151 + MSM6295 path
- **Protection:** "suicide battery" on the Q/B-board backing display/priority
  config; tamper or battery death bricks the board
- **Notable titles (all Capcom):** Cadillacs and Dinosaurs, The Punisher,
  Warriors of Fate, Saturday Night Slam Masters (Muscle Bomber)
- **Notes:** The architectural bridge to CPS-2 — QSound and Kabuki-encrypted
  sound arrive here first, while the main CPU stays open.

---

## CPS-2 — CP System II (1993)

- **Main CPU:** 68000 @ 16 MHz, in a custom Capcom package (DL-1525), running
  **fully encrypted** program code
- **Sound CPU:** Z80 @ 8 MHz, "Kabuki" encrypted variant (Capcom DL-030P)
- **Sound chip:** **QSound** @ 4 MHz — 16 PCM channels (~24 kHz) plus ADPCM,
  3D positional
- **Graphics:** same CPS-A/CPS-B lineage (RICOH A5C-series custom @ 16 MHz, with
  VTI/Hitachi gate arrays DL-1625 / DL-2227)
- **Display:** 384×224 (512×262 overscan); up to 65,536-color palette,
  ~2,048 on-screen; **900 sprites** (16×16, scalable to 256×256); 3 scroll layers
- **Protection:** full 68000 encryption + suicide batteries; region encoded by
  the **coloured plastic shell** (A-board and B-board region must match)
- **I/O:** JAMMA+ "kick harness" (34-pin) extends to 6 buttons per player —
  shared with NAOMI I/O and CPS-3
- **Notable titles:** Super Street Fighter II / Turbo, Street Fighter Alpha
  (Zero) 1–3, X-Men: Children of the Atom, Marvel Super Heroes, X-Men vs Street
  Fighter, **Marvel vs Capcom**, Darkstalkers/Vampire series, Cyberbots,
  Armored Warriors (Powered Gear), Battle Circuit, **Progear**, D&D: Tower of
  Doom / Shadow over Mystara, Alien vs Predator, 19XX, Giga Wing, Mars Matrix
- **Emulation note:** the suicide-battery encryption tables were the long-standing
  obstacle; CPS-2 is now fully understood, but driver identity is inseparable from
  the per-game decryption key.

---

## CPS-3 — CP System III (1996)

- **Main CPU:** Hitachi **SH-2**, shipped as the custom Capcom **DL-3229 SCU**
  (a decapped HD6417099 SH-2 variant) with **built-in encryption**; commonly
  cited at 25 MHz (the on-die decryption stage runs ~6.25 MHz internally)
- **Storage:** game on **SCSI CD-ROM**, flashed at first boot into Flash ROM
  SIMMs (8 × 16 MiB) on the motherboard; later units shipped pre-flashed without
  the CD drive
- **Sound:** custom **16-channel 8-bit stereo** sample player (no QSound, no Z80)
- **Graphics (2D only, but the most advanced 2D Capcom built):** 32,768 colors
  (15-bit 555), **1,024 objects with hardware scaling**, 4 scroll layers + a text
  overlay layer, framebuffer zoom, linescroll/linezoom, color-blend effects
- **Display:** 384×224 (4:3) or 496×224 (wide)
- **Protection:** per-game **security cartridge** holding the BIOS, the SH-2's
  decryption logic, and the key in battery-backed **ferroelectric RAM** (RAMTRON
  FM1208S). Extremely tamper/static-sensitive — the key erases on disturbance,
  bricking the cart. Street Fighter III: 2nd Impact is the exception, with default
  keys that re-write onto dead carts at boot.
- **Notable titles (six, all fighting games):** Red Earth / Warzard, Street
  Fighter III: New Generation / 2nd Impact / 3rd Strike, JoJo's Venture / JoJo's
  Bizarre Adventure
- **Notes:** Capcom's last proprietary board, and a commercial flop — a premium
  2D platform launched into the 3D era. Encryption reverse-engineered by
  Andreas Naive in 2007, which is what made emulation possible.

---

## Post-CPS — outsourced 3D platforms

- **Capcom ZN-1 / ZN-2 (1995–1998):** a **Sony PlayStation-derived** board
  (R3000-class) Capcom shared with other publishers under the "ZN" umbrella.
  Used for Capcom's 3D experiments: Street Fighter EX series (developed by Arika),
  Star Gladiator, Rival Schools. A stopgap, not a CPS successor.
- **Sega NAOMI (adopted 1999):** after CPS-3, Capcom abandoned proprietary
  hardware and moved to Sega's **Dreamcast-based NAOMI** (SH-4) for Marvel vs
  Capcom 2, Power Stone, and many later titles — covered in the Sega factsheet.

---

## Architectural through-line (for emulator authors)

Four observations that matter for structuring CPU cores and drivers — directly
relevant if CPS-1 is a pilot subsystem:

1. **CPS-1 and CPS-2 share one video architecture** (CPS-A / CPS-B). CPS-2 adds
   full program encryption and routes sprite access through dedicated sprite RAM
   tied into the security logic, but the tilemap/sprite/palette model is the same.
   A correct CPS-1 graphics core is most of a CPS-2 core once decryption is in
   place — so CPS-1 is the right place to start, and it pays forward.

2. **Audio is three distinct eras:** YM2151 + MSM6295 (CPS-1) → QSound DSP
   (Dash, CPS-2) → custom 16-channel sample chip (CPS-3). The Z80 sound CPU
   survives through CPS-2 (encrypted "Kabuki" from Dash onward) and then vanishes
   on CPS-3, where the SH-2 owns audio.

3. **Protection is the real driver-identity axis,** more than the CPU. The line
   escalates: CPS-B config-in-RAM (late CPS-1) → Kabuki sound encryption (Dash) →
   full 68000 encryption + suicide batteries (CPS-2) → SH-2 built-in decryption +
   FeRAM security cart (CPS-3). Per-game keys are part of the machine definition,
   not an afterthought.

4. **Main-CPU progression:** 68000 @ 10 → 12 → 16 MHz across CPS-1/1.5/2, then a
   jump to **SH-2** at CPS-3 (and onward to Sony/PS and Sega/SH-4 hardware once
   Capcom stopped building its own). One well-built 68000 core covers three of the
   four CPS generations; CPS-3 is the clean architectural break.

---

## Sources

Cross-referenced from: System16 (The Arcade Museum) CPS/CPS2/CPS3 hardware pages;
MAME source (`src/mame/capcom/*.cpp`, esp. `cps1.cpp`/`cps2.cpp`/`cps3.cpp`);
Wikipedia (CP System, CP System II, CP System III); Arcade Hacker's CPS-1
teardown series; Arcade Otaku and gendev/SpritesMind hardware threads; and the
CPS Changer console documentation.
