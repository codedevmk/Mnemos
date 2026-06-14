---
id: ADR-0028
title: "Capcom CPS1 Arcade Subsystem (classic YM2151+OKI first, QSound deferred)"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-14
ratified: null
---

# ADR 0028: Capcom CPS1 Arcade Subsystem (classic YM2151+OKI first, QSound deferred)

## Context

CPS1 has been on the roster since the v0.1 project plan and is named in every
multi-system planning document as the first Capcom arcade family. ADR-0012
built the Irem M72 arcade subsystem *as the foundation CPS1 would reuse*: it
states outright that "the arcade ROM-set/DIP/orientation infrastructure that
CPS1/CPS2 will reuse unchanged" is delivered by the M72 work. M72 is now at
parity (the authentic R-Type set renders title and gameplay), so the arcade
foundation is proven and CPS1 enters scope.

This ADR fixes how CPS1 maps onto the existing tier architecture **before any
chip is built** — the same gating ADR-0011 applied to the 32X and ADR-0012
applied to M72 — and records the scope boundary (which board variants are in,
which are deferred) and the one genuinely new architectural rule the family
introduces: the CPS-B per-game configuration is *hardware-profile data*, not a
C++ game driver.

The cannibalization source is a sibling C codebase's clean-room CPS1 core
(written from public hardware documents and community logs), the same provenance
relationship ADR-0006 governs for every other ported core. The port follows the
established cannibalization precedent (M72's V30, Genesis/Sega CD's m68000): port
behaviour against the reference, gate real-ROM conformance behind an env-gated
corpus, and translate the C into idiomatic Mnemos C++ — never vendor a
third-party core wholesale.

### Hardware summary (public documentation)

The Capcom Play System 1 (1988) is built from:

- **Motorola 68000** main CPU (~10–12 MHz).
- **Zilog Z80** sound CPU (~3.58 MHz).
- **Yamaha YM2151 (OPM)** FM synthesiser on the Z80's ports.
- **OKI MSM6295** 4-channel ADPCM sample player on early/classic boards; late
  boards replace the MSM6295 with a **QSound** DSP and sample-ROM storage.
- **CPS-A and CPS-B** custom chips: CPS-A is largely the address/scroll/object
  register file and the palette-DMA engine; CPS-B carries layer enable, layer
  priority, palette control, a 16×16→32 multiplier "math box", a board
  identity/security value, and — critically — a **per-board-revision scrambled
  register map**, so the same logical CPS-B register sits at different physical
  offsets on different board configurations.
- A unified GFX RAM holding three scrolling tilemap layers (8×8, 16×16 with
  per-line row-scroll, 32×32), an object/sprite list, and palette RAM; 4 bpp
  planar tiles; a 384×224 display at ~59.6 Hz with a vblank IRQ and, on some
  configurations, a programmable raster-compare IRQ.

The 68000 memory map (classic board), from the clean-room reference:

| Range | Function |
|---|---|
| `$000000–$7FFFFF` | Program ROM |
| `$800000–$80001F` | Input ports |
| `$800030` | Coin control |
| `$800100–$80013F` | CPS-A registers (a write to register 5 triggers a palette DMA) |
| `$800140–$80017F` | CPS-B registers (profile-dispatched) |
| `$800180–$80018F` | Sound command latch + timer/fade latch |
| `$900000–$92FFFF` | Unified GFX RAM (tile / sprite / palette) |
| `$FF0000–$FFFFFF` | 64 KB main work RAM |

The Z80 map: `$0000–$7FFF` program ROM, `$8000–$BFFF` a banked window, `$D000`
2 KB work RAM, `$F000/1` YM2151, `$F002` MSM6295, `$F004` bank select, `$F006`
MSM6295 pin-7 rate, `$F008` sound-latch read, `$F00A` timer/fade latch.

## Decision

### No new top-level trees — the placement catalog and ADR-0012 already cover this

Per ADR-0001/0009/0012, the pieces land where their capability already lives.
The arcade infrastructure ADR-0012 created is consumed verbatim.

| Piece | Canonical Mnemos home | Status |
|---|---|---|
| Main CPU | `src/chips/cpu/m68000` | **exists** (Genesis, Sega CD) — reused unchanged |
| Sound CPU | `src/chips/cpu/z80` | **exists** (M72) — reused unchanged |
| FM synth | `src/chips/audio/ym2151` | **exists** (M72) — reused unchanged |
| Sample DAC | `src/chips/audio/dac8` | **exists** — available if a path needs it |
| MSM6295 ADPCM | `src/chips/audio/okim6295` | **new** |
| CPS-A/CPS-B video | `src/chips/video/cps_a_b` | **new** |
| CPS-B profile + gfx-mapper data | a data library beside `cps_a_b` (or `manifests/capcom_cps1`) | **new** |
| Arcade ROM-set loader | `manifests/common` (`mnemos-romset/1`) | **exists** (M72) — reused unchanged |
| DIP / cabinet input / orientation | `peripheral_sdk` + manifest schema + player | **exists** (M72) — reused unchanged |
| Scheduler (integer + rational dividers) | `runtime::scheduler` | **exists** — reused unchanged |
| Board family | `src/manifests/capcom_cps1/` (board manifest + per-game TOML) | **new** |
| Frontend | `src/apps/player/adapters/capcom_cps1/`, self-registered family id | **new** |

The genuinely new code is two chips, one data library, a board manifest, and a
player adapter — roughly the same delta the 32X and Sega CD ports carried, but
smaller because the entire CPU/sound/loader/adapter stack is already in place.

### Game drivers are data, not code — and so is the CPS-B profile

ADR-0012 established that a game is a TOML manifest layered on the board
manifest, never C++. CPS1 stresses this rule harder than M72 because of the
CPS-B scrambled register map: each board configuration needs a different set of
register offsets, layer-enable masks, a priority table, a multiplier
offset, a board-identity value, and a graphics-code mapper. That data is
structurally complex, and the temptation is to encode it as a C++ table keyed by
game.

**The decision: the CPS-B configuration is *hardware-profile* data, named by
board/chip identity, never by game.** A profile (`profile_xx`) and a graphics
mapper (`mapper_xx`) describe *board wiring* — many games share one profile. The
per-game TOML selects them by name (`cps_b_profile = "profile_xx"`,
`gfx_mapper = "mapper_xx"`); it never carries a C++ branch on a game. If a
profile cannot be defended as board/chip wiring, its data stays in manifest
data, not code. The profile *library* may be a constexpr table for compile-time
validation and zero-cost lookup, but its keys are hardware identities and its
selection is data — so "game drivers are data not code" holds structurally. This
is the single most important and most error-prone decision in the family: one
wrong offset silently corrupts exactly one profile's layer order/priority/palette
while every other game looks correct, so the profile data gets its own
reviewable drop and a per-profile table-driven test matrix.

### CPS-A and CPS-B are one `ivideo` chip with an internal split

CPS-A and CPS-B are physically separate chips, but the renderer consumes their
combined register state every frame. Exposing them as two public chips would
manufacture an artificial synchronization edge and a second save-state boundary
for no behavioural gain — the same reason M72 models its whole board video as a
single `ivideo` unit. CPS1 video is therefore **one chip, `src/chips/video/
cps_a_b`**, built like `irem_m72_video`: span-attached GFX/palette/object memory,
`ivideo` contract, introspection from day one.

Internally the implementation splits into register-file, profile, and renderer
helpers. Externally, introspection exposes **separate CPS-A and CPS-B register
snapshots** so the debug surface still reflects the hardware boundary.

### One authoritative GFX RAM; palette latched at the CPS-A DMA write

The reference eagerly mirrors GFX-RAM writes into three side buffers
(tile/sprite/palette) gated by a test-only flag, alongside the real palette-DMA
path, and carries test-scaffold address aliases. The clean port models **one
authoritative GFX-RAM buffer with computed tile/sprite views** and the
**palette-DMA path only**, dropping the eager mirror and the scaffold aliases.
The hardware caveat this surfaces is recorded as a contract: **palette state is
latched at the CPS-A register-5 DMA write, not read live** from GFX RAM, so
software that writes palette RAM without triggering the DMA renders from the last
latched palette — matching the hardware.

### Rendering altitude: frame-at-vblank for v1, raster-accuracy is a later increment

M72 renders the whole frame at vblank and reaches parity. CPS1 has per-line
row-scroll and (on some configurations) a raster-compare IRQ, both of which imply
mid-frame register/RAM effects that a frame-at-once renderer cannot express.
**v1 renders at vblank like M72 and explicitly scopes raster-sensitive
correctness out of scope.** Line-latched / raster-accurate rendering is a later,
explicit increment, not a silent assumption — affected state (row-scroll tables,
scroll registers) is line-latched only when that increment lands.

### Determinism: lean on the scheduler; serialize all board glue; seed the clock

- The Z80↔68000 handoff uses the **existing rational/integer scheduler**
  (ADR-0005); the board does not invent a second board-local sync clock unless a
  handoff behaviour is found that the scheduler cannot express. If any fractional
  accumulator is introduced it is 64-bit integer and serialized.
- **All board glue participates in save-state**: sound latch, timer/fade latch,
  Z80 bank, MSM6295 pin-7/bank, CPS-A/CPS-B registers, multiplier operands and
  result, IRQ pending/enable, the sprite double-buffer latch, the palette-DMA
  latch/cache, DIP/input latches.
- The one configuration carrying an M48T35 timekeeper RTC reads host time in the
  reference; Mnemos **injects a fixed/seeded clock** so the parity-sweep
  methodology stays deterministic. That configuration (and its raster IRQ) is a
  later increment, not a v1 blocker.

### QSound is deferred — and not faked

The late QSound boards replace the YM2151+MSM6295 audio path with a DSP-based
sample engine. Mnemos has no DSP16-class core, and adding one would balloon scope
before anything renders. **v1 ships the classic YM2151+OKI sound system only**
(the strong majority of the early roster). QSound board configurations are marked
**unsupported in the TOML/adapter load paths** — surfaced as an explicit
unsupported-config error, never silently mis-emulated. The audio topology stays
pluggable so the YM2151+OKI assumption does not leak into the adapter's audio
drain; QSound (plus the DSP core) gets its own follow-up ADR.

### Encrypted sound sets are a prerequisite, not a free increment

Some later sets encrypt the Z80 sound program and require the CPU to serve opcode
fetches from a separately decrypted image. The current `z80` exposes a single
fetch path, not a public opcode/data-bus split. Encrypted-set support is
therefore a **prerequisite `z80` API task**, evaluated on its own merits if and
when those sets are pursued — it is **not** assumed as a board sub-increment, and
is out of scope for v1.

### Build order (phase-gated, one reviewable drop each)

Each phase is a single reviewable drop, unit-tested against a mock bus or
synthetic fixtures; **no real ROMs are required until the final phase**, which is
env-gated.

- **Phase 1 — OKIM6295 chip.** `src/chips/audio/okim6295`, modelled on
  `rf5c68`: the 4-channel Dialogic-ADPCM core, the 1-byte-stop / 2-byte-play
  command FSM, the phrase-table reader off a host-set sample-ROM span,
  capture-sink + introspection, `register_factory` self-registration. Unit-tested
  against a synthetic sample ROM with both reference-parity and independent
  invariants (phrase bounds, FSM state, stop/play overlap, busy gating,
  high-nibble-first decode, predictor/index clamps, pin-7 rate, status nibble,
  save/load mid-sample). Reusable beyond CPS1. **First drop.**
- **Phase 2 — `cps_a_b` video chip.** The `ivideo` skeleton + colour decode,
  then the three tilemaps, then the object list, then priority and flip-screen.
  Includes a **2.0 profile skeleton (one synthetic profile)** so priority work is
  built against the profile interface from the start, not retrofitted. Tested
  against synthetic tile/sprite/palette RAM with sentinel pixels.
- **Phase 3 — CPS-B profile + gfx-mapper data library.** The full profile and
  mapper data set as hardware-identity-keyed data, with a per-profile
  table-driven test matrix (ID readback, register-index placement, per-bank
  graphics-code → bank assertions). No `% tile_count` universal fallback —
  bank-edge and power-of-two wrap are tested per profile.
- **Phase 4 — `capcom_cps1` board manifest.** Assemble the reused CPUs + YM2151
  + new OKIM6295 + `cps_a_b` on a 24-bit big-endian main bus and a 16-bit sound
  bus; MMIO CPS-A/CPS-B/I-O/DIP/sound-latch windows; vblank IRQ; the CPS-A reg-5
  palette-DMA side effect; per-game `board_params`. Big-endian MMIO byte-lane
  behaviour (byte vs word, odd/even, open-bus, reg-5 side effects) is tested
  explicitly. Hand-assembled synthetic 68000 programs under the real scheduler;
  first per-game `games/*.toml`.
- **Phase 5 — Player adapter + family wiring.** `apps/player/adapters/
  capcom_cps1` mirroring the M72 adapter (load_set, assemble, build_schedule,
  input packing, YM+OKI audio drain), self-registering the `capcom_cps1` family
  id; the shared touch-points (player CMake, `main.cpp` force-link,
  `system_family` enum/name/label).
- **Phase 6 — Env-gated real-ROM conformance.** Per-game `[data]` tests
  (region sizes/population, auto-detected profile/orientation/DIP, reset-vector
  sanity, runs-N-frames-without-halting) and a playable smoke matrix — skipped
  cleanly when the corpus is absent, never a hard fail.

## Deferred

- **QSound entirely**, including the DSP core and the late-board audio map; its
  own follow-up ADR.
- **Encrypted Z80 sound sets** and the `z80` opcode/data-bus-split API they need.
- **Raster-accurate / line-latched video** and the raster-compare IRQ
  configuration; v1 is frame-at-vblank.
- **The M48T35 timekeeper configuration**; lands later with a seeded clock.
- **Cycle-exact 68000/video timing** — first increment is instruction-stepped
  with documented cycle costs, like every prior CPU phase.
- **CPS2** — a separate family and ADR once CPS1 is at parity.

## Consequences

- Mnemos gains a reusable **OKIM6295** chip (used well beyond CPS1 — other
  ADPCM-based arcade boards) and the **CPS-B profile/gfx-mapper data**
  infrastructure that CPS2 will extend.
- Adding the second, third, nth CPS1 game is a TOML file naming a ROM set, a
  CPS-B profile, a graphics mapper, an input profile, DIP defaults, and an
  orientation — not C++. The board/game split is enforced structurally: the board
  contains only board behaviour; the profile library contains only hardware-keyed
  wiring data.
- The base consoles and the M72 family are untouched: no existing chip, bus,
  scheduler, manifest, or adapter changes except additive new modules. The
  arcade infrastructure ADR-0012 created is exercised a second time, validating
  its "CPS1/CPS2 reuse it unchanged" claim.
- ROMs and conformance corpora are never committed; boot/parity tests skip
  without them, exactly as the Genesis/Sega CD/32X/M72 suites do. Parity is a
  claim validated only where those artifacts are present.
- No third-party emulator names or game titles appear in code or comments;
  hardware is described directly, attribution stays in `THIRD-PARTY.md`, and ROM
  set identity lives in data/provenance surfaces only.

## Ratification

Proposed 2026-06-14 from the CPS1 bring-up pilot session, grounded in a
parallel map of the reference core and the Mnemos arcade templates and a
read-only architecture review (the OKIM6295-first ordering, the single
`cps_a_b` chip, the profile-as-hardware-data rule, the frame-at-vblank scope,
and the QSound deferral were all stress-tested before this record). Awaiting
owner ratification per MNE-CTX-PLAN-001 §5.
