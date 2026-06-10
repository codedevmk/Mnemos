# ADR 0012: Irem Arcade Subsystem (M72 first, M92 later)

**Status:** Proposed
**Date:** 2026-06-10

## Context

Arcade hardware has been on the Mnemos roster since the v0.1 project plan
(CPS1/CPS2 appear in every multi-system planning doc), but no arcade family has
entered scope yet. This ADR assesses the first one: Irem boards, scaffolded by
**board family, not by game** — M72 first, M92 once M72 is stable. It fixes how
the work maps onto the existing tier architecture before anything is built, the
same gating ADR-0011 applied to the 32X.

The proposal that prompted this ADR sketched a MAME-shaped standalone tree
(`emulators/irem/{common,m72,m92}/`, `cpu/{v30,v33,z80}/`, `sound/{ym2151,dac,
ga20}/`, `arcade/{machine,scheduler,address_space,rom_loader,...}`) with an
`IArcadeMachine` interface, an `IremM72Board` class, and declarative per-game
driver structs. The *architecture* of that sketch — board family below game
driver, shared devices below board devices, no game hacks in the board — is
exactly right and is adopted here. The *layout* is not: most of the proposed
infrastructure already exists in Mnemos under different names, and ADR-0009's
placement catalog assigns a canonical home to every remaining piece. This ADR
records that mapping and the (short) list of genuinely new capabilities arcade
support requires.

### Hardware summary (public documentation)

- **Irem M72** — NEC V30 main CPU (32 MHz/4 = 8 MHz), Z80 sound CPU
  (32 MHz/8 = 4 MHz), YM2151 FM synth clocked off a separate 3.579545 MHz
  crystal, a DAC fed sample data by the sound program, two scrolling tilemap
  layers plus sprites at 384×256 around 55 Hz, a programmable scanline-compare
  raster IRQ, and a sound latch between the CPUs. Several M72 games carry an
  i8751 MCU for protection and sample serving; **R-Type does not**, which is
  why it is the first target.
- **Irem M92** — V33 main CPU plus a second NEC V-series sound CPU (real
  boards add per-game opcode encryption on the sound CPU), YM2151 plus the
  Irem GA20 4-channel PCM chip, and the GA21/GA22 custom graphics chips. A
  materially bigger jump; deferred until M72 is at parity.

## Decision

### No new top-level trees — the placement catalog already covers this

The proposed `emulators/`, `cpu/`, `sound/`, and `arcade/` roots are not
created. Per ADR-0001/0009, the pieces land where their capability already
lives:

| Proposed | Canonical Mnemos home |
|---|---|
| `cpu/v30`, `cpu/v33` | `src/chips/cpu/v30` — one NEC V-series core; V33 is a configured variant (chips are arg-less constructed, configured after) |
| `cpu/z80` | `src/chips/cpu/z80` — **already exists**, full instruction set incl. undocumented ops |
| `sound/ym2151` | `src/chips/audio/ym2151` |
| `sound/dac` | `src/chips/audio/dac` |
| `sound/ga20` | `src/chips/audio/ga20` (M92 phase) |
| M72 tilemap/sprite video | `src/chips/video/irem_m72_video` (one coherent `ivideo` unit, like the VDPs) |
| M92 GA21/GA22 | `src/chips/video/irem_ga21_ga22` (M92 phase) |
| `arcade/machine.h` (`IArcadeMachine`) | `frontend_sdk::player_system` — already exists; its doc comments already anticipate an arcade adapter publishing System/Board/Game |
| `arcade/scheduler.h` | `runtime::scheduler` (ADR-0005) |
| `arcade/address_space.h` | `topology::bus` + manifest `[[bus.region]]` / `[[mmio_block]]` |
| `arcade/save_state.h` | ADR-0008 save-state contract |
| `arcade/trace_logger.h` | `instrumentation` `trace_target` / `memory_view` / `debug_layer` — "add trace compare hooks immediately" is already satisfied by the chip contract |
| `arcade/input_ports.h`, `dip_switches.h` | `peripheral_sdk` + manifest schema extension (new work, below) |
| `arcade/rom_loader.h` | arcade ROM-set loader in `manifests/common` (new work, below) |
| `emulators/irem/m72/m72_board.*` (`IremM72Board`) | `src/manifests/irem_m72/` — board manifest TOML + callbacks bundle, the Genesis/Sega CD pattern |
| `emulators/irem/m72/games/rtype.cpp` (driver structs) | per-game TOML manifests layered on the board manifest |
| frontend glue | `src/apps/player/adapters/irem_m72/`, self-registering family id via the adapter registry |

### Game drivers are data, not code

The sketched `static IremM72GameDriver rtype { .roms = {...}, .inputProfile =
..., .dipProfile = ... }` becomes a per-game TOML manifest
(`rtype.toml`) referencing the shared board manifest (`m72.toml`). Mnemos is
already declarative at the system level (`mnemos-manifest/1`); a game is a
thinner layer of the same idea: ROM set, orientation, input profile, DIP
defaults, banking/protection callback ids. The directive "do not hardcode game
hacks into the board" is thereby enforced structurally — the board callbacks
bundle contains only board behaviour; anything per-game must be expressible as
manifest data or a named per-game callback.

### The board family is the manifest, games are leaves

The layering in the proposal maps one-to-one:

```
GameDriver            →  src/manifests/irem_m72/games/rtype.toml
BoardFamily M72       →  src/manifests/irem_m72/ (m72.toml + irem_m72_callbacks)
Shared devices        →  src/chips/{cpu/v30, cpu/z80, audio/ym2151, audio/dac}
Board devices         →  src/chips/video/irem_m72_video + board callbacks (IRQ ctl, banking, sound latch)
Frontend              →  src/apps/player/adapters/irem_m72/ behind player_system
```

### V30 follows the established CPU porting policy

`src/chips/cpu/v30` is built the way m68000 and sh2 were: an instruction-
stepped `icpu` (`step_instruction()` returns cycle cost, `tick()` catches up by
whole instructions), memory through the abstract `ibus` with **little-endian**
16-bit accesses, `register_view` + `trace_target` introspection from day one.
Per ADR-0006, "use an existing proven core" means *port against a proven
reference and gate behind an env-gated conformance corpus* (never committed,
CTest `SKIP_RETURN_CODE 4` when absent) — not vendoring a third-party core
wholesale, which would also collide with the license split (ADR-0003). The V33
lands later as a configuration of the same core (timing table + the V33-only
behaviours), the same way one m68000 serves Genesis and Sega CD.

Note: topology recently grew wide **big-endian** bus accessors for the
SH-2/68000 hot path; the V30 needs the little-endian counterpart. That is an
additive tier-3 change.

### Genuinely new capabilities (the actual arcade delta)

Everything above is reuse. Four things do not exist yet and are the real scope
of "arcade support":

1. **Arcade ROM-set loader** (`manifests/common`). Console systems load one
   cart image; an arcade game is a *set* of dumps assembled into regions —
   even/odd byte-interleaved pairs for the 16-bit V30 program, separate tile,
   sprite, and sample regions — each file CRC32/SHA-verified, typically inside
   a zip. The 2026-05-27 plan explicitly deferred "arcade-ROM bundle
   abstractions ... when CPS enters scope"; Irem triggers that work now. The
   manifest schema grows multi-file region support (per-file offset, stride
   for interleave, per-file hash); the loader composes the existing
   `compression` (zip) and `security/cryptography` (CRC32/SHA) modules per
   ADR-0009 — no new hashing or unzip code.

2. **A second clock domain.** The fixed-divider scheduler (ADR-0005) assumes
   every chip is an integer divider of one master clock. M72's V30 (/4) and
   Z80 (/8) divide the 32 MHz master exactly, but the YM2151's 3.579545 MHz
   crystal does not. Two options, decided before the audio phase (CPU/video
   phases are unaffected):
   - **Recommended: per-chip rational divider** — a numerator/denominator
     accumulator on the existing scheduler. Integer-only state, fixed dispatch
     order, fully deterministic; a small additive extension.
   - Item 4B (slice-based multi-clock scheduler) — correct but overkill for
     one audio chip; remains gated on Saturn/32X-class needs.

3. **DIP switches and cabinet inputs.** `peripheral::controller_state` covers
   stick + buttons; arcade adds coin, start, service, and test inputs plus DIP
   banks read through input ports. Manifest schema gains `[input]` / `[dip]`
   blocks (port bit layout, factory defaults, option labels); `peripheral_sdk`
   gains the arcade input device; the player maps keys for coin/start and gets
   a DIP settings surface. DIP state participates in save-state.

4. **Display orientation.** Image Fight and Air Duel are vertical (TATE)
   games. `frontend_sdk::video_region` carries only a refresh rate; it grows
   an orientation field the player honours by rotating presentation. R-Type
   and Mr. Heli are horizontal, so this can land mid-roster rather than up
   front.

### M72 board behaviours stay at the board layer

The scanline-compare raster IRQ, vblank IRQ, sprite-DMA trigger, sound latch
(+ Z80 INT/NMI plumbing), program-ROM banking, and flip-screen are wired in
`irem_m72_callbacks` via the existing `[[mmio_block]]` / IRQ-callback pattern
the Genesis manifest already uses — the scheduler stays rendering-agnostic and
IRQs cross chips through callback-set lines, exactly as ADR-0005 prescribes.

### Protection is an MCU, not metadata

Mr. Heli, Image Fight, and Air Duel carry an i8751 whose program participates
in boot checks and sample serving. That is a CPU, so per the chip contract it
is eventually `src/chips/cpu/mcs51` with the per-game MCU ROM declared in the
game manifest — not behaviour simulated inside the board callbacks. Games
needing it stay out of the roster until the chip exists (or a game-manifest
declared simulation callback is consciously accepted as an interim, marked as
such). R-Type needs none of this.

### Build order (phase-gated, one reviewable drop each)

The proposed 7-step order survives almost intact, re-cut to the repo's phase
idiom:

- **Phase A — V30 chip.** Core + conformance harness + introspection. Unit-
  testable against a mock `ibus` before any board exists (ADR-0004 affordance).
- **Phase B — ROM-set loader + board manifest.** Manifest schema extension,
  M72 memory map (ROM, work RAM, video/sprite/palette RAM, sound latch, I/O
  ports), R-Type set loads and verifies. First visual artifact: a raw tile/
  sprite-region viewer exposed as introspection `debug_layer`s through the
  existing `--screenshot` path — no new tooling.
- **Phase C — Video + IRQs.** Tilemap layers, sprites, priority, scrolling,
  flip-screen, palette; scanline/vblank IRQ wiring; R-Type attract mode
  renders and becomes the screenshot-parity baseline.
- **Phase D — Audio + inputs.** YM2151 chip, DAC, sound latch, rational-
  divider scheduler extension; coin/start/DIP inputs; R-Type playable
  end-to-end.
- **Phase E — Roster.** Mr. Heli / Image Fight / Air Duel as data-only game
  manifests plus whatever Phase E exposes as genuinely missing (mcs51,
  orientation). Each new game proves the board/game split held.
- **Phase F — M92 ADR.** V33 config, GA20 (`chips/audio`), GA21/GA22
  (`chips/video`), sound-CPU opcode decryption. Not planned in detail here;
  gets its own ADR when M72 is at parity.

## Deferred

- **Cycle-exact V30 timing** (prefetch queue depth, EA timing edge cases) —
  first increment is instruction-stepped with documented cycle costs, like the
  m68000 and SH-2 first phases.
- **M92 entirely**, including GA20/GA21/GA22 and sound-CPU decryption.
- **mcs51 (i8751) chip** — gated to Phase E at the earliest.
- **Item 4B slice scheduler** — not pulled in by M72; the rational-divider
  extension covers the YM2151 crystal.

## Consequences

- Mnemos gains reusable chips (V30 family, YM2151, DAC — all useful beyond
  Irem; YM2151 alone covers a long list of future arcade and home targets) and
  the arcade ROM-set/DIP/orientation infrastructure that CPS1/CPS2 will reuse
  unchanged.
- Adding the second, third, nth M72 game is a TOML file, not C++ — the
  declarative-driver goal of the original proposal, delivered through the
  manifest layer that already exists.
- ROMs, MCU dumps, and conformance corpora are never committed; boot/parity
  tests skip without them, exactly as the Genesis/Sega CD/32X suites do.
  Parity remains a claim validated only where those artifacts are present.
- The base consoles are untouched: no existing chip, bus, scheduler, or
  manifest path changes except the additive extensions named above (LE wide
  bus accessors, rational divider, multi-file ROM regions, input/DIP schema),
  each of which lands with its own tests.
