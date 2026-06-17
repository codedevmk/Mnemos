> **L3 design note** (2026-06-10): normative content lifted to `constitution/`
> per `constitution/MIGRATION.md` (MNE-CTX-PLAN-001 P0, ADR-0013). On any
> divergence, `CONSTITUTION.md` and the constitution modules take precedence.

# Mnemos — Todos

**Version:** 0.1 (initial draft)
**Status:** In progress — M0 and M1 complete; M2 substantially complete; M3 core complete (the first golden boot is the only gap, data-gated on local C64 ROMs).
**Companion documents:** `mnemos-architecture-tds-v0.1.md`, `mnemos-project-plan-v0.1.md`

---

## How to Use This File

- Tasks are grouped by milestone (M0 through M10).
- M0–M3 are itemized to actionable granularity.
- M4–M10 are itemized to scoping granularity; each will be expanded to actionable items at the start of its milestone.
- A task is "done" when its acceptance criterion is met and its work has landed in a green CI run.
- Tasks are intentionally not assigned. Assignment happens in your task tracker, not in this file.
- When a task is completed, change `[ ]` to `[x]` and add a short reference to the PR or commit.

---

## M0 — Workspace and Toolchain Bring-Up

### Repository setup
- [x] Initialize monorepo `mnemos/` with the directory layout from TDS §5.
- [x] Add `LICENSE` (Apache-2.0) at root.
- [x] Add `LICENSE-chips` (MIT) at root.
- [x] Add `THIRD_PARTY_NOTICES.md` (empty template).
- [x] Add `README.md` with one-paragraph vision blurb and pointer to docs.
- [x] Add `CONTRIBUTING.md` (placeholder; populated by M9).
- [x] Add `CODE_OF_CONDUCT.md`.
- [x] Add `SECURITY.md` (placeholder).

### Source control hygiene
- [x] Add `.gitignore` that rejects `build/`, `*.log`, `*.bin` (firmware), IDE caches, vcpkg/Conan artifacts.
- [x] Add `.gitattributes` (line endings, binary classifications).
- [x] Add `.editorconfig`.

### Formatting and linting
- [x] Add `.clang-format` (LLVM base + project overrides; column limit, brace style decided here).
- [x] Add `.clang-tidy` with curated check list (modernize-*, bugprone-*, performance-*, readability-* subset).
- [x] Add pre-commit hook script (`tools/git-hooks/pre-commit`) running clang-format dry-run.
- [x] Add `tools/install-hooks.sh` and `tools/install-hooks.ps1`.

### Build system
- [x] Author root `CMakeLists.txt` with project declaration, C++23 baseline, global warning flags.
- [x] Author `CMakePresets.json` covering all eight presets from TDS §6.3.
- [x] Author `cmake/modules/MnemosDeclareTier.cmake` exposing `mnemos_declare_tier()`.
- [x] Author `cmake/modules/MnemosCompilerFlags.cmake` with the per-platform flag bundle from TDS §6.4.
- [x] Author `cmake/modules/MnemosTesting.cmake` providing `mnemos_add_test()`.
- [x] Enforce out-of-source builds at root CMakeLists.
- [x] Add empty `extern/` with `README.md` documenting FetchContent usage convention.

### Foundation skeleton
- [x] Create `src/foundation/CMakeLists.txt` declaring `mnemos::foundation` as a header-only target with one no-op header.
- [x] Add one trivial unit test under `src/foundation/tests/` proving Catch2 wiring works.
- [x] FetchContent integration for Catch2 v3 in `cmake/modules/`.

### Continuous Integration
- [x] Choose CI provider (GitHub Actions is the default; deviation requires an ADR).
- [x] Add `.github/workflows/ci.yml` with matrix: {windows-latest, ubuntu-24.04} × {Debug, Release}.
- [x] CI runs configure + build + test for each preset.
- [x] CI fails on any compiler warning.
- [x] CI runs `clang-format --dry-run` and fails on diff.
- [x] Add CI status badge to `README.md`.

### Documentation seed
- [x] Create `docs/architecture/mnemos-architecture-tds-v0.1.md` (this TDS, committed verbatim).
- [x] Create `docs/architecture/mnemos-project-plan-v0.1.md` (this plan, committed verbatim).
- [x] Create `docs/architecture/mnemos-todos-v0.1.md` (this file, committed verbatim).
- [x] Create `docs/adr/0001-monorepo-layout.md` recording the layout decision.
- [x] Create `docs/adr/0002-cmake-and-toolchain.md` recording build system choices.
- [x] Create `docs/adr/0003-license-split.md` recording Apache+MIT rationale.

### Acceptance
- [x] A "trivial change" PR (touching one comment) passes the full CI matrix on both platforms with zero warnings. (fad647b: CI run 26275205022 green across all 7 jobs on windows-latest + ubuntu-24.04, incl. clang-format gate)

---

## M1 — Foundation Library and First Chip

### Foundation library
- [x] Implement `foundation::time` (steady_clock wrappers, frame-time helpers). (this commit: windows-msvc-debug configure/build/test green)
- [x] Implement `foundation::log` (leveled, structured, sinks). (d00f7e3 + de38104; CI run 26277600708 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::bits` (popcount, clz, byte order, bitfield helpers). (e0ca6d2 + 8ed9bd7; CI run 26278425815 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::span_ext`, `foundation::expected_ext` (light extensions to std). (1cc1c1d + c5d71b6; CI run 26279089811 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::fs` (filesystem facade with platform-safe path handling). (5ecbe01; CI run 26279447600 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::thread` (jthread wrappers, latches, signals — std-based). (f04befe; CI run 26279663407 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::allocator` (arena, pool — minimal v0.1 surface). (13cbec5 + ce90a3e; CI run 26280113028 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::id` (compile-time string IDs / fnv1a hashing for chip IDs). (7ac85be; CI run 26280299754 green across clang-format and all 6 build/test jobs)
- [x] Unit tests for every module above with ≥ 75 % branch coverage. (1d20324; CI run 26280879101 green across clang-format, foundation coverage, and all 6 build/test jobs)

### Chip taxonomy and base interfaces
- [x] Create `src/chips/common/` library target `mnemos::chips::common`. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `chip_class` enum. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `chip_metadata`, `reset_kind`, `register_descriptor` types. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `ichip` interface (TDS §8.2). (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define specialized interfaces: `icpu`, `iaudio_synth`, `ivideo`, `ibus_controller`, `istorage`, `imapper`, `iperipheral`. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `ichip_introspection` interface (forward-decl OK; full def in M4). (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Implement chip factory registry with static-init registration pattern. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Unit tests for taxonomy + registration. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)

### MOS 6510 implementation
- [x] Create `src/chips/cpu/m6510/` library target `mnemos::chips::cpu::m6510`. (72e4a6e; CI run 26320527793 green across clang-format and all 6 build/test jobs)
- [x] Implement registers, flags, addressing modes. (250a457; CI run 26321421738 green across clang-format and all 6 build/test jobs)
- [x] Implement all 151 documented opcodes with cycle counts. (250a457; CI run 26321421738 green across clang-format and all 6 build/test jobs)
- [x] Implement all undocumented (illegal) opcodes the C64 software relies on. (this commit; stable set: LAX/SAX/DCP/ISC/SLO/RLA/SRE/RRA/ANC/ALR/ARR/SBX, SBC alias, and undocumented NOPs. Unstable SHA/SHX/SHY/TAS/LAS/ANE/LXA and JAM/KIL deferred — see src/chips/cpu/m6510/NOTES.md)
- [x] Implement decimal mode arithmetic. (39eb5fe; CI run 26321421738 green across clang-format and all 6 build/test jobs)
- [x] Implement IRQ, NMI, RES handling with correct cycle semantics. (this commit; IRQ/NMI are cycle-accurate 7-cycle sequences with masking + NMI edge-latch, RES is functional via reset(); interrupts polled at the instruction boundary — exact mid-instruction timing edge cases validated later by the conformance ROMs)
- [x] Implement the 6510 I/O port at addresses $00/$01. (8bb1bda; CI run 26320828290 green across clang-format and all 6 build/test jobs)
- [x] Implement save / load state. (landed with the M3 runtime save-state format; m6510::save_state/load_state in m6510.cpp; round-trip tested in m6510_test.cpp)
- [x] Implement introspection (register snapshot, PC, cycle counter, instruction event tap). (this commit; register_snapshot()/elapsed_cycles()/cpu_registers() on m6510. The instruction-event tap rides with the M4 instrumentation interface, which is minimal in M1 by design)
- [x] Register factory under `"mos.6510"`. (72e4a6e; CI run 26320527793 green across clang-format and all 6 build/test jobs)

### Test infrastructure for 6502 family
- [x] Vendor or fetch a functional test corpus with provenance documented. (superseded by the public per-cycle 6502 corpus per ADR 0006 — a stronger conformance gate; provenance in src/chips/cpu/m6510/tests/README.md)
- [x] Vendor or fetch decimal mode test. (covered by the per-cycle corpus: decimal ADC/SBC/ARR validated across the corpus)
- [x] Vendor or fetch undocumented opcode test suite. (covered by the per-cycle corpus: stable illegals validated; unstable/JAM out of scope per NOTES.md)
- [x] Author `m6510_conformance_test` integrating all of the above. (daf7aba; `m6510_conformance_test`, ~2.4M vectors pass locally. Data-gated; kept local-only per decision — corpus never committed, skips in CI)
- [x] Author per-opcode microtests (cycle count, flag updates) for at least the trickiest 20 opcodes. (the m6510_test suite covers page-cross, RMW dummy-write, indirect-JMP bug, decimal, branch timing, interrupts, illegals)

### Acceptance
- [~] `m6510_conformance_test` passes on all four matrix combinations. Validated locally against the full public per-cycle 6502 corpus across Win/Linux × Debug/Release behavior; CI execution kept local-only per ADR 0006 (corpus never committed). CI runs it as a skipped, data-gated test.
- [x] Zero warnings, zero sanitizer hits when run under ASan+UBSan on Linux Clang. (d328480; CI run 26324986992 `linux-clang-asan` green)
- [x] ADR `docs/adr/0004-chip-contract.md` records the contract decisions made during this milestone. (0c... see docs/adr/0004-chip-contract.md)

---

## M2 — Chip Library Expansion (C64 Set)

**Status:** Substantially complete. All five C64 chips (6510, VIC-II, SID, CIA, PLA)
are implemented and unit-tested; the VIC-II scanline renderer covers standard +
multicolour text + bitmap (hi-res + multicolour) + extended-colour text + border +
the full 8-sprite engine (multicolour, X/Y expansion, priority, and sprite-sprite /
sprite-data collisions) — all five display modes. Per-chip save/load landed with
the M3 save-state format. Remaining: SID register-trace validation against a known
tune, and strictly cycle-exact VIC beam timing (tracked with B4).

### VIC-II 6569
- [x] Create `src/chips/video/vic_ii_6569/` library target. (a013df0; ported from Emu per ADR 0006; CI run 26325382940 green across all jobs)
- [x] Implement raster engine (PAL: 312 lines × 63 cycles). (a013df0; beam tracker + per-cycle video-matrix counters, PAL/NTSC geometry; CI run 26325382940)
- [x] Implement all graphics modes (standard text, multicolor text, standard bitmap, multicolor bitmap, extended color text). (render_line dispatches all five valid modes from the ECM/BMM/MCM bits — hi-res + multicolour text, standard bitmap (screen-RAM nibble colours), multicolour bitmap (bg0/screen-hi/screen-lo/colour-RAM), and extended-colour text (code bits 6-7 pick bg0-3); the ECM+BMM/ECM+MCM invalid combos display black. Each mode feeds the sprite foreground mask. Unit-tested per mode. Only strictly cycle-exact beam/X alignment and per-cycle splits remain, tracked with B4)
- [x] Implement sprite engine (8 sprites, multicolor, expansion, sprite-sprite/sprite-data collisions). (render_sprites composites all 8 MOBs onto each scanline after the background: sprite-pointer + 63-byte data fetch, hi-res (24px) and multicolour (12 dots) pixels, X/Y expansion, per-sprite + shared multicolour colours, and sprite-background priority resolved against a per-pixel foreground mask the background renderer now emits. Sprite-sprite ($D01E) and sprite-data ($D01F) collisions accumulate per frame and raise the IMMC ($04) / IMBC ($02) IRQ sources on the first collision after each read-clear. 7 unit tests (position, disabled, multicolour, X+Y expand, priority front/behind, both collision types). Sub-cycle beam-exact sprite/idle fetch values remain bounded by the scanline renderer, as noted under B4)
- [~] Implement border generation (open borders trick must work). (display-window vs border decided per pixel from DEN latch + CSEL/RSEL geometry; the open-border / sprite-in-border tricks need cycle-exact beam state and are pending)
- [x] Implement raster IRQ generation. (a013df0; edge-latched raster compare + mask + master + write-1 ack + light-pen source; CI run 26325382940)
- [x] Implement badline cycle stealing. (a013df0; bad-line condition + BA-low + CPU-read-stall windows; CI run 26325382940)
- [x] Implement MMIO register set ($D000–$D3FF mirrored). (a013df0; full $D000-$D02E path, $D02F-$D03F read $FF, 64-byte mirror; CI run 26325382940)
- [x] Save / load state. (landed with the M3 runtime save-state format; vic_ii_6569::save_state/load_state in vic_ii_6569.cpp; round-trip tested in vic_ii_6569_test.cpp)
- [x] Introspection: register snapshot, current raster line, current cycle, sprite states. (a013df0; register_snapshot + raster_y/raster_x; full sprite-engine states arrive with the sprite compositor)

### SID 6581
- [x] Create `src/chips/audio/sid_6581/` library target. (a7dc3f8; ported from Emu per ADR 0006; CI run 26387148559 green across all jobs)
- [x] Implement register interface ($D400–$D7FF mirrored). (a7dc3f8; 32-byte alias, write-only/read-only classes)
- [x] Implement three voices with ADSR. (a7dc3f8; datasheet rate table + exponential decay/release divider, gate re-trigger)
- [x] Implement waveform generators (triangle, sawtooth, pulse, noise, combined). (a7dc3f8; 23-bit noise LFSR, combined-AND, 8580 partial-restore)
- [x] Implement filter (resonance + cutoff; v0.1 acceptable as "audible and stable", quality refinement is its own task). (a7dc3f8; state-variable LP/BP/HP, per-variant range)
- [x] Implement ring modulation and sync. (a7dc3f8)
- [x] Implement read-back of OSC3 and ENV3. (a7dc3f8)
- [x] Save / load state. (landed with the M3 runtime save-state format; sid_6581::save_state/load_state in sid_6581.cpp; round-trip tested in sid_6581_test.cpp)
- [x] Introspection: register snapshot, voice states, filter state. (a7dc3f8; envelope/volume snapshot + waveform_output/voice_phase/envelope accessors)
- [x] Reference: at least one published SID emulation note set documented in `src/chips/audio/sid_6581/NOTES.md`. (a7dc3f8; MOS 6581 datasheet + Yannes interview per Emu PROVENANCE)

### CIA 6526
- [x] Create `src/chips/bus_controller/cia_6526/` library target. (91172fb; ported from Emu per ADR 0006; CI run 26386882167 green across all jobs)
- [x] Implement two parallel ports (A and B). (91172fb; DDR + live host callbacks + PB6/PB7 timer output)
- [x] Implement two timers (A and B) with all modes (one-shot, continuous, cascaded). (91172fb; incl. force-load + start-delay pipelines, φ2/CNT/TA-cascade input modes)
- [x] Implement TOD clock. (91172fb; 50/60 Hz divider, BCD with AM/PM, alarm, latched read, write freeze)
- [x] Implement serial shift register. (91172fb; input + output modes with SDR-complete IRQ)
- [x] Implement IRQ generation and mask. (91172fb; NMOS edge-triggered IR flip-flop + 1-φ2 pin delay + read-clear)
- [x] Save / load state. (landed with the M3 runtime save-state format; cia_6526::save_state/load_state in cia_6526.cpp; round-trip tested in cia_6526_test.cpp)
- [x] Unit tests covering timer mode transitions. (91172fb; continuous/one-shot/cascade + NMOS imr edge + TOD/alarm/SDR; full Lorenz-suite validation is M3 system integration)

### C64 PLA (memory banking)
- [x] Create `src/chips/mapper/c64_pla/` library target. (76acf1f; ported from Emu per ADR 0006; CI run 26386496106 green across all jobs)
- [x] Implement LORAM/HIRAM/CHAREN/GAME/EXROM decoding to the 14-state banking table. (76acf1f; decode_cpu_address/decode_vic_address across standard/8K/16K/ultimax; CI run 26386496106)
- [x] Wire to bus region overlay control. (src/manifests/c64 — assemble_c64 maps RAM/BASIC/KERNAL/CHARGEN/I-O overlays whose active predicates call decode_cpu_address with the live 6510 $01 port; c64_system_test exercises every bank state)

### Acceptance
- [~] Each chip's unit tests pass in CI. (VIC-II, SID, CIA, PLA all green; M2 not fully complete — the VIC-II open-border trick and SID tune-trace validation remain)
- [ ] SID register interface validated against a reference register trace from at least one known SID tune. (deferred to M3 C64 integration — needs a player + trace)
- [x] No regressions in M1's 6510 tests. (m6510 suite green in CI run 26387148559 alongside the new chips)

---

## M3 — Topology, Manifest Loader, Runtime, and First Boot

**Status:** Core complete and CI-green. Done: the topology bus, the manifest
loader + system builder, the runtime scheduler + frame-tagged input, full
save-state (zstd container + CRC32 + rewind ring), the headless CLI, the fully
wired C64 (IRQ/NMI routing, dynamic VIC bank, IEC bus), and the complete 1541 —
both the protocol-level synthetic drive and the cycle-accurate full drive (6502 +
2x 6522 VIA + GCR), selectable via `--drive-rom` (B6 + B9 done), plus the C64
keyboard/joystick/paddle input (B2), cartridges (B5), the datasette (B7), the
REU (B8), region/SID/dual-SID selection (B10), the unstable opcodes + `$01` fade
(B11), the cartridge/expansion-port open-bus latch (B13), and ADR 0005 (scheduler
strategy), the RS-232 userport modem (B12), and the VIC open-bus reads (B4). The
entire Emu-gap B-list (B1-B13) is now closed; the only residual is strictly
cycle-exact per-cycle VIC data-bus values, which ride with the deferred
sprite/bitmap engine rather than a gap item. The one milestone gap is the first
golden boot test, data-gated on local C64 ROMs — the CLI + framebuffer-hash
pipeline is ready.

### Topology library
- [x] Create `src/topology/` library target `mnemos::topology`. (7e62c0d; tier-3 library implementing chips::ibus; CI run 26387396049 green)
- [x] Implement `bus` with width, endianness, sorted region table. (7e62c0d)
- [~] Implement region kinds: `ram`, `rom`, `mmio_chip`, `mapper`. (7e62c0d: ram/rom/mmio done; mapper backing arrives with the mapper-hook slice)
- [x] Implement bus read/write fast path with cached resolution. (7e62c0d; O(log N) sorted-table resolution. Per-chip cached backing pointer is a later optimization)
- [ ] Implement mapper hook plumbing. (cartridge mappers — deferred until a cart is needed; the no-cart C64 boot does not use one. The overlay predicate mechanism can host a mapper view when added.)
- [x] Implement overlay control (PLA-driven). (d88a651; region priority + per-access active predicate — ROM read-overlays with write-fallthrough, I/O overlays; the C64 shell drives the predicates from c64_pla. CI run 26387620584)
- [x] Unit tests covering region resolution edge cases (boundary, mirror, overlay precedence). (7e62c0d boundary + mirror; d88a651 overlay precedence with ROM/IO banking)

### Manifest loader
- [x] Create `src/manifests/common/` library target `mnemos::manifests::common`. (010640b; CI run 26387908879 green)
- [x] FetchContent integration for `tomlplusplus`. (010640b; pinned v3.4.0, PRIVATE to tier 4, ADR 0007)
- [x] Define `manifest_schema/1` types in C++. (010640b; manifest/clock/chip/bus/region + address_range)
- [x] Implement TOML parser with strict validation (TDS §10.3). (010640b; toml++ non-throwing + schema/field/range/endianness/rom checks)
- [x] Implement manifest-to-component-graph builder (instantiate chips by ID via the factory registry, wire buses). (9da1028; build_system creates chips via create_chip, builds topology buses, allocates RAM, binds MMIO via immio, attaches CPUs; CI run 26388745058)
- [x] Implement ROM file loader with SHA-256 verification. (9da1028; rom_provider + foundation::sha256 verification with mismatch diagnostics)
- [x] Surface validation errors with file/line/column. (010640b; diagnostic{message, source, line, column})
- [x] Unit tests covering each validation rule. (010640b; valid parse, wrong schema, missing id/clock/bus, malformed-TOML position, range + rom requirements)

### C64 manifest
- [x] Create `src/manifests/c64/` directory. (tier-4 `mnemos::manifests::c64`: c64_system assembler + concrete-chip wiring, sidestepping factory-id static-init stripping)
- [x] Author `c64.pal.toml` per TDS §10.2. (chip `type` ids mos.6510/6569/6581/6526/906114; clock dividers; RAM + three ROM overlay regions)
- [x] Document ROM acquisition and SHA-256s in `src/manifests/c64/ROMS.md`. (acquisition, per-file hash commands, placeholder-sha256 policy; tests use pattern-filled buffers, no copyrighted data)
- [x] Note: ROM files themselves are NOT committed; CI obtains them from a configured source. (`.gitignore` ignores `*.bin`/`roms/`; manifest sha256 fields are placeholders pinned locally per ROMS.md)

### C64 system wiring & peripherals (from the Emu C64 gap review)
The C64 chips are individually well-ported; these are system-level wiring/peripheral
gaps the Emu review surfaced.
- [x] Route VIC + CIA1 /IRQ into the 6510 /IRQ and CIA2 into /NMI. (B1; assemble_c64 ORs vic.irq_asserted()|cia1 via edge callbacks into set_irq_line, CIA2 -> set_nmi_line; the single biggest correctness blocker — without it no IRQ-driven program runs)
- [x] Track the VIC 16K bank from CIA2 port A at runtime. (B3; CIA2 write_port_a -> vic.set_bank((~port_a_pins())&3))
- [x] Keyboard matrix + joystick wiring (CIA1 PRA/PRB callbacks) and paddle/POT mux to SID. (B2; c64_input models the 8x8 matrix + both joysticks + both paddle pairs, wired into CIA1 PRA/PRB in assemble_c64; the paddle mux (CIA1 PRA bit7->port1, bit6->port2) routes the selected pair to SID POTX/POTY ($D419/$D41A). Matrix scan, joystick overlay, and paddle mux all unit + integration tested. Feeding the frame-tagged input_buffer / a frontend into c64_input is a runner/frontend concern, not chip wiring)
- [x] VIC floating-bus / open-bus read semantics (unmapped I/O returns last VIC fetch). (B4; the expansion-port floating bus at $DE00-$DFFF is done under B13 via vic_ii_6569::last_fetched_byte(). The VIC register-window open bus is now complete: reads of the unused register bits return 1 as the silicon does — $D016 | $C0, $D018 | $01, the 4-bit colour registers $D020-$D02E | $F0 (joining the already-modelled $D019/$D01A high bits and the $D02F-$D03F = $FF aliases). The Mnemos VIC is more accurate here than the Emu reference, whose read path left those bits unmasked. The only residual is the strictly cycle-exact per-cycle data-bus value (e.g. the exact sprite/idle fetch byte visible to a mid-fetch read), which needs the cycle-exact beam the scanline renderer does not model — tracked with the deferred sprite/bitmap engine, not as a separate gap)
- [x] Cartridge support: `.crt` loader + generic 8K/16K/Ultimax + Ocean/Magic Desk + EasyFlash; drive `/GAME`//EXROM into the PLA. (B5; chips::mapper::c64_cartridge parses .crt + banks ROML/ROMH + I/O bank-switching; assemble_c64 maps ROML $8000 / ROMH $A000+$E000 / cart I/O $DE00 gated by the PLA, and feeds the cart's /GAME//EXROM into the decode; CLI --cart. The cart I/O-2 open-bus latch is the separate B13)
- [x] Disk: IEC serial bus + synthetic 1541 (devices 8-11) + `.d64` reader for protocol-level LOAD. (B6; chips::iec_bus + chips::storage::c1541::{d64_image, synthetic_drive} + CIA2 IEC wiring in assemble_c64 + CLI --disk. Command/serving logic unit-tested; the bit-level handshake is ROM-gated like the golden boot)
- [x] Datasette: 1530 `.tap` v0/v1 pulse playback -> CIA1 /FLAG. (B7; chips::storage::datasette parses .tap v0/v1, counts down each pulse and pulses CIA1 /FLAG; motor from $01 bit5, cassette sense on $01 bit4 via the new m6510 set_port_input. Wired in assemble_c64; CLI --tape auto-presses PLAY. Unit + integration tested)
- [x] REU (1700/1764/1750) `$DF00` DMA controller. (B8; chips::peripheral::reu — 128/256/512 KiB expansion RAM, the $DF00 register file (status/command/C64+REU address/length/IRQ-mask/address-control) and stash/fetch/swap/verify DMA with optional fixed-address and autoload. Wired into assemble_c64 as a $DF00 I/O-2 overlay (priority above the cartridge window) gated on c64_config::reu; CLI --reu <128|256|512>; included in the save state when enabled. The completion IRQ is status-poll only — the /IRQ line is not yet wired to the CPU (follow-up). Unit + C64 integration tested)
- [x] Full cycle-accurate 1541 (6502 + 2x 6522 VIA + GCR) and the MOS 6522 VIA chip. (B9; via_6522 + chips::storage::c1541::{gcr, disk_bind, full_drive} — drive 6502 (port-disabled m6510) + 2x VIA + 2K RAM + 16K DOS ROM + GCR head/stepper/SYNC + IEC auto-ATN-ack. Memory map / VIA wiring / mechanism unit-tested with a synthetic ROM; real DOS ROM is data-gated and the GCR read path tracks Emu's unfinished state)
- [x] System-level SID variant / NTSC region / dual-SID selection + an NTSC manifest. (B10; assemble_c64 takes a c64_config — region sets VIC revision + phi2 + mains TOD, sid_variant picks 6581/8580, dual_sid maps a second SID at $D420 (priority overlay over the SID mirror). c64.ntsc.toml added; CLI derives region from the manifest id and takes --sid/--dual-sid. Verified PAL vs NTSC produce different frame hashes)
- [x] 6510 unstable illegal opcodes (SHA/SHX/SHY/TAS/LAS/ANE/LXA) + `$01` bit 6/7 floating-gate fade. (B11; decoded + executed deterministically — ANE/LXA with a fixed magic ($EE), SHA/SHX/SHY/TAS store source & (high+1), TAS sets SP=A&X, LAS = mem & SP; the $01 bits 6,7 fade to 0 after switching to input. Page-cross address corruption not modelled. Unit-tested)
- [x] RS-232 / userport modem (CIA2 PA2 TXD + userport, Emu `c64_modem.c`). (B12; full bridge in four parts. chips::peripheral::modem ports Emu's Hayes "AT" core (command/online modes, dial, +++ escape, S-registers, result codes) behind a pluggable modem_transport — loopback + a live tcp_transport (Winsock/BSD sockets). chips::peripheral::rs232 is the bit-level userport UART (TXD sampling, RXD shifting, /FLAG start-bit, configurable baud). assemble_c64 wires them to CIA2 (PA2=TXD, PB0=RXD, /FLAG, byte sink/source); CLI --modem (loopback) / --dial host:port (TCP, auto-ATDT). modem + UART unit-tested in isolation and integration-tested through CIA2; the tcp_transport is compiled on all platforms but not dialed in CI, and matching the KERNAL's programmed baud end-to-end is data-gated on the ROM)
- [x] Cartridge I/O-2 ($DF00) open-bus "last byte" latch. (B13; the VIC-II now latches the last byte it fetched off the main bus (vic_ii_6569::last_fetched_byte(), $FF until the first fetch, saved in the VIC state). assemble_c64 maps an open I/O-1/I/O-2 overlay at $DE00-$DFFF — priority 1, above base RAM but below the cartridge (2) and REU (3) — so in I/O mode with no cart/REU a read returns that floating-bus byte (the stale value fastloaders/protection probe) and the no-op write keeps the PLA-deselected RAM from being clobbered; an inserted cart still answers $FF for addresses it does not decode, matching Emu's vic_last_fetch model. VIC unit + C64 integration tested (open bus overrides RAM, mirrors the live latch, yields to cart/REU). Sub-cycle exactness is bounded by the scanline renderer, as with B4)

### Runtime library
- [x] Create `src/runtime/` library target `mnemos::runtime`. (compiled tier-5 static lib)
- [x] Implement master clock with divider table. (scheduler tracks the master cycle; each chip carries a master->chip divider)
- [x] Implement fixed-divider scheduler dispatching per-chip ticks. (scheduler dispatches tick() per cycle in chip order, with a lockstep fast path when all dividers are 1; TDS §11.2)
- [x] Implement frame-tagged input buffer. (input_buffer keeps events sorted by frame for deterministic replay; CIA1 keyboard/joystick wiring is follow-up)
- [x] Implement frame boundary detection and signaling. (run_frame / run_frames advance until the designated ivideo frame_index increments)
- [x] Implement save state with header + per-chip chunks (TDS §15). (runtime::write_save_state / read_save_state: MNMS header + per-chip + per-memory chunks; per-chip save_state/load_state implemented for all 5 C64 chips; unknown chunks skipped for forward-compat)
- [x] FetchContent integration for `zstd`. (ADR 0008; pinned v1.5.6, SOURCE_SUBDIR build/cmake, libzstd_static linked PRIVATE into tier-5 runtime)
- [x] Implement save state compression (zstd) and decompression. (level-3 frame around the chunk body)
- [x] Implement CRC32 trailing checksum. (foundation::crc32 over header+compressed body, verified on load)
- [x] Implement rewind ring (configurable depth, default 600 frames). (runtime::rewind_ring; full states, at_or_before seek, oldest-evict)
- [x] Unit tests for scheduler dispatch, save/load roundtrip, rewind. (scheduler dispatch/dividers/frames + input buffer + save_state round-trip/corruption/mismatch/unknown-chunk + rewind seek/evict/clear; per-chip save/load round-trips in each chip's test)

### Headless runtime CLI
- [x] Create `tools/mnemos_runtime_cli/` executable target. (tier-8 `mnemos_runtime_cli` + shared `mnemos_runtime_cli_core` lib so tests reuse the logic)
- [x] CLI options: `--manifest`, `--rom-dir`, `--frames`, `--dump-hash`, `--save`, `--load`, `--input-log`. (all implemented; `--save` writes a save state after the run, `--load` restores before it. `--input-log` replays a frame-tagged text script — `<frame> press/release <key>`, `joy1/joy2 <dirs|none>`, `paddle1/paddle2 <x> <y>`, `releaseall`, with `#` comments — parsed into a runtime::input_buffer and applied to the C64 keyboard/joystick/paddle at the start of each frame via scheduler::run_frame(). parse_input_log is unit-tested (keys/joystick/paddle/comments + malformed-line rejection) and validated end-to-end through the CLI binary)
- [x] Outputs framebuffer hash (SHA-256 of RGBA bytes) per frame or at end. (at end via `--dump-hash`; pixels serialised R,G,B,A for a cross-platform-stable hash. Loads the manifest, reads/verifies ROMs from `--rom-dir` — placeholder sha256 -> unverified warning — assembles the C64, runs `--frames`, hashes. Verified deterministic end-to-end with synthetic ROMs.)

### First golden test
- [x] Author `tests/golden/c64_basic_boot_test.cpp`. (harness landed: locates ROMs from MNEMOS_C64_ROM_DIR (sized filename candidates), boots a cartridge-free C64 mirroring the CLI's reset + scheduler order, renders MNEMOS_C64_BOOT_FRAMES (default 200) frames, and hashes the framebuffer via the shared hash_framebuffer. Self-skips via CTest SKIP_RETURN_CODE 4 when ROMs are absent; with ROMs present it asserts determinism + visible output and prints the hash; with MNEMOS_C64_BOOT_SHA256 set it asserts the golden. tests/ wired into the build under BUILD_TESTING; see tests/golden/README.md)
- [~] Boot C64 manifest with no cartridge, run frames + hash + assert golden. (mechanics done and validated with synthetic ROMs; capturing/committing the real golden hash + frame count is the data-gated step, performed once real C64 ROMs are available locally)
- [ ] Commit the expected hash. (data-gated: record MNEMOS_C64_BOOT_SHA256 from a real-ROM run, then bake it into the test/CTest env)
- [x] CI runs this on every PR. (registered as mnemos_c64_basic_boot_test; runs every job and reports "Skipped" until ROMs are provided)

### Acceptance
- [ ] `mnemos_runtime_cli load c64.pal --frames 600 --dump-hash` produces an identical hash on Win+Linux, Debug+Release.
- [x] Save → load → continue produces an identical hash trajectory from the load point onward. (verified via the CLI: load@frame 3 + 3 frames matches a straight 6-frame run; master cycle restored)
- [x] ADR `docs/adr/0005-scheduler-strategy.md` records the fixed-divider choice and its limitations. (records the fixed-divider master-clock model, lockstep fast path, rendering-agnostic frame detection, and the v0.1 limitations: no sub-cycle bus arbitration / static ratios only)

---

## M4 — Instrumentation API and Developer Frontend MVP

**Status:** In progress — the debugger foundations (the pure, testable instrumentation
core) are **complete** in the new compiled tier-6 `mnemos::instrumentation::api`
library: iruntime_introspection + a concrete `debugger` with execution control,
PC breakpoints, memory watchpoints, and event subscription, all unit-tested against
a real m6510. What remains in M4 is the frontend MVP — the Cap'n Proto wire
protocol/transport/server, Vulkan + UI toolkit primitives, and `apps::dev` — which
is much larger and platform-heavy.

- [ ] Promote `ichip_introspection` from forward decl to full interface. (Phase 1 reads CPU state via injected probes so no chip-contract change was forced yet; a richer ichip_introspection — register/memory views — lands with the memory-inspection work)
- [x] Implement `iruntime_introspection`. (interface + concrete `debugger`: master_cycle/frame_index queries, step_instruction / step_frame / run-with-instruction-budget control, PC breakpoint + memory watchpoint management, and event subscription. The async pause/resume pair from the TDS sketch is a live-frontend-thread concern that lands with the wire server; the deterministic headless surface is complete)
- [x] Implement breakpoint engine (PC, memory R/W, conditional). (PC breakpoints with optional per-hit conditions + enable/disable/remove + a stop_event report, checked at instruction boundaries; unit-tested against a real m6510 running a small program)
- [x] Implement watch engine. (memory read / write / access watchpoints over an address range, with an optional value condition, via a new generic topology::bus access-observer hook the debugger installs — null by default so the hot path is a single branch and the golden boot is unchanged. The hit halts run() at the end of the triggering instruction with a watchpoint stop_event. Unit-tested: write vs read discrimination, value condition, range, and no-bus = never fires)
- [x] Implement event subscription with filters. (event_sink + an event_filter mask over event_kind {breakpoint, watchpoint, step, frame}; subscribe/unsubscribe on the debugger deliver each emitted event — with the id/pc/master_cycle/frame snapshot — to every subscriber whose filter selects its kind. Emitted from run (breakpoint/watchpoint halts), step_instruction (step or watchpoint), and step_frame (frame). Unit-tested: delivery, multi-kind filter, exclusion, unsubscribe)
- [ ] Author Cap'n Proto schemas under `src/instrumentation/wire/`.
- [ ] Build-time codegen for C++ wire bindings.
- [ ] Implement wire transport (named pipe on Windows, domain socket on Linux).
- [ ] Implement in-process wire server.
- [ ] Bring up Vulkan rendering primitives in `frontend_sdk` (minimum needed for dev frontend).
- [ ] Bring up custom UI toolkit primitives (windowing, panels, text).
- [ ] Build `apps::dev` with disassembly, memory view, registers, breakpoints, controls, framebuffer mirror.
- [ ] ADR for breakpoint expression language.

---

## M5 — Player Frontend MVP

(Scoping-level items; expand at milestone start.)

- [ ] Expand `frontend_sdk` UI toolkit (lists, theming, asset loader, input mapping).
- [ ] Build `apps::player` with library view, launch, framebuffer presentation, audio, input, save/load, pause/reset.
- [ ] Configurable library root directory.
- [ ] Basic per-game save-state slots.
- [ ] Multiplatform gamepad input (XInput on Windows, evdev / SDL_gamepad on Linux).
- [ ] Audio output (WASAPI on Windows, PipeWire/PulseAudio on Linux).

---

## M6 — Sega Master System

**Status:** In progress — the Z80 CPU, SN76489 PSG, optional YM2413 FM unit, SMS VDP, and Sega mapper have
landed, `manifests::sms::assemble_sms` wires them into a **bootable SMS**, and the
headless CLI now **runs SMS carts** (`--manifest sms.{ntsc,pal}.toml --cart game.sms
--frames N --dump-hash`) through the master-clock scheduler with a deterministic
framebuffer hash. The whole stack fit with **zero changes to icpu / ibus / any
lower-tier contract**. The SMS golden-boot harness is verified locally against a
cartridge image at a pinned framebuffer hash; the Z80 exerciser harness is in place but
not yet run against a real exerciser image (none is in the local corpus). Both SMS
mappers (Sega + Codemasters) are implemented. The SMS FM unit is available through
`sms_config::fm_unit` and `mnemos_player --fm`, routing ports $F0/$F1/$F2 into the
existing YM2413 chip and mixing its capture path in the player. What's left for M6 is purely data-gated
(the golden boot + the Z80 exerciser run) — the code is complete.

- [~] Implement `chips::cpu::z80` (full undocumented behavior; passes ZEXALL). (chips::cpu::z80, ported from the Emu reference per ADR 0006 and improved: the per-T-state wait machinery dropped, the non-portable anonymous-union register pairs replaced with portable 16-bit members + inline half accessors. Complete instruction set — 256 unprefixed + CB + ED (block transfer / block I/O / 16-bit arith) + DD/FD (IX/IY) + DDCB/FDCB, the common undocumented opcodes (SLL, IX/IY halves) and the full flag model incl. the XF/YF bits. Memory via ibus; the separate Z80 I/O space via injected port callbacks; NMI + IM0/1/2; instruction-stepped with a catch-up tick(); save/load + register_snapshot; factory "zilog.z80". 16 unit tests across LD/ALU/INC/16-bit/JP/CALL+RET/PUSH+POP/CB/ED-LDIR/DD/IN+OUT/NMI/IM1/tick. ZEXALL/ZEXDOC conformance now has a data-gated harness — src/chips/cpu/z80/tests/z80_conformance_test.cpp runs a .com exerciser in a minimal CP/M environment (TPA load at $0100, BDOS fn 2/9 console stub trapped at $0005, warm-boot exit at $0000) and asserts no "ERROR"; SKIP without MNEMOS_Z80_TEST_ROM. The harness is validated end-to-end with a synthetic .com; running the real exerciser to flip this to [x] is local/data-gated — the .com image is not in the local corpus)
- [x] Implement `chips::audio::sn76489`. (chips::audio::sn76489, ported from the Emu reference: 3 square-tone channels + 1 LFSR noise channel, the -2 dB/step attenuation table, the latch/data write port, white/periodic noise with the positive-edge LFSR clock, and an optional 1-pole analog low-pass. step() emits one mono sample; tick() drives it through the chip's internal /16 prescaler. save/load + register_snapshot; factory "ti.sn76489". 8 unit tests (silence on reset, latch tone/volume, square wave at peak, attenuation level, LFSR reset, tick divider, state round-trip). Shared with the Genesis PSG (M8))
- [x] Implement `chips::video::sms_vdp`. (chips::video::sms_vdp, ported from the Emu reference: Mode-4 scanline renderer — 32x28 name table with per-tile priority/palette/flip, 4bpp planar tiles, full-screen H/V scroll with the row/column locks, 64 sprites (8x8 / 8x16, optional zoom, 8-per-line limit with the overflow + collision status flags), left-column blanking. The two-byte control-port command protocol (VRAM read/write, register write, CRAM write), the buffered data-port read, V/H counters, and the line + frame interrupts via a callback. CRAM (--BBGGRR) renders into an 0x00RRGGBB framebuffer; as an ivideo frame source it ticks per Z80 cycle (228/line) and bumps frame_index per frame. immio (data/ctrl), save/load; factory "sega.sms_vdp". 8 unit tests (identity, reset geometry, register write, buffered VRAM r/w, a Mode-4 tile render, the frame IRQ + status-read clear, frame-index advance, state round-trip))
- [x] Implement SMS mapper (Sega mapper, Codemasters mapper). (chips::mapper::sms_mapper, ported from the Emu reference per ADR 0006 and improved: span-based borrowed ROM, std::array cart RAM, no debug-event coupling. The Sega mapper — three 16 KiB ROM slots (slot 0's first 1 KiB fixed to physical page 0), the optional 16 KiB on-cart RAM window at slot 2 with a bank-select bit, page wrap modulo the image page count, and the four control registers at $FFFC-$FFFF (fully decoded — the $DFFC-$DFFF RAM mirror is ignored). cpu_read/cpu_write for the $0000-$BFFF banked window + write_register for the register window; save/load (registers + cart RAM) + register_snapshot (CTRL/PAGE0-2); factory "sega.sms_mapper". 8 unit tests. The Codemasters mapper is a separate chip (chips::mapper::codemasters_mapper, factory "codemasters.mapper"): three fully-banked 16 KiB slots with page registers in ROM space ($0000/$4000/$8000), no fixed first 1 KiB, power-on pages 0/1/0, and the 8 KiB on-cart RAM mapped at $A000-$BFFF when bit 7 of the $4000 write is set. Spec from published community hardware notes, cross-checked against independent open-source Sega emulators (absent from the Emu reference). 7 unit tests (identity+factory, 0/1/0 power-on, three-slot banking with offset preserved, whole-slot-0 banking, page wrap, cart RAM enable/read-write/disable, state round-trip))
- [x] Author `manifests/sms/` (NTSC + PAL). (manifests::sms::assemble_sms wires the Z80 + SMS VDP + SN76489 + optional YM2413 FM + Sega mapper into a bootable machine. Memory map: $0000-$BFFF banked ROM/cart-RAM via the mapper, $C000-$DFFF 8 KiB work RAM mirrored to $E000-$FFFF, with the $FFFC-$FFFF mapper-register overlay writing through to RAM. Z80 I/O routing: $00-$3F open-bus reads / $3F I/O-control latch writes, $40-$7F V/H counters (read) + PSG (write), $80-$BF VDP data/ctrl, `$F0/$F1/$F2` YM2413 address/data/audio-select when `sms_config::fm_unit` is enabled, and $C0-$FF joypad ports $DC/$DD with the active-low pin + TH/TR nationalisation model. The VDP /INT line is ORed into the Z80 IRQ; SP is set to $DFF0 to emulate the post-BIOS hand-off. sms_config selects NTSC (262 lines) vs PAL (313). System tests cover boot Z80->RAM, region, mapper banking through the bus, VDP OUT, PSG OUT, optional FM OUT/IN, joypad IN, and an end-to-end frame-IRQ ISR run). assemble_sms auto-selects the cartridge mapper: the
  Codemasters checksum header ($7FE6 word + $7FE8 complement == $10000) picks the
  Codemasters mapper, else the Sega mapper; sms_config::mapper can force either. The
  bus is wired accordingly (Codemasters pages via ROM-space writes with no $FFFC
  overlay; Sega uses the overlay). 3 added tests cover detect / default / force)
- [x] SMS headless CLI run path. (mnemos_runtime_cli now branches on the manifest
  family: a `sega` manifest takes the SMS path — reads the cart from --cart, derives
  NTSC/PAL from the manifest id, assembles the machine, and runs frames through a
  {VDP, CPU, PSG} master-clock scheduler (VDP as the frame source). Ships
  sms.ntsc.toml / sms.pal.toml; --dump-hash emits the deterministic framebuffer
  SHA-256. CLI tests: a sega manifest + cart runs 1 frame with a stable hash across
  two passes, and a missing --cart errors. save/load + input-log stay C64-only for now)
- [~] Golden frame tests for known commercial SMS ROMs. (tests/golden/sms_boot_test.cpp:
  a self-skipping golden-boot harness mirroring the C64 one — assembles the SMS from
  a .sms cart, runs N frames through the {VDP,CPU,PSG} scheduler, and asserts the
  framebuffer SHA-256. Env: MNEMOS_SMS_ROM (cart path), MNEMOS_SMS_REGION (ntsc/pal),
  MNEMOS_SMS_BOOT_FRAMES (default 200), MNEMOS_SMS_BOOT_SHA256 (golden). SKIP_RETURN_CODE
  4 → CTest "Skipped" without a ROM; with one it checks non-uniform output + cold-boot
  determinism + the golden hash. Verified locally against a cartridge image (NTSC, 200
  frames) — the boot framebuffer matches its pinned golden on two independent cold
  boots; the golden hash itself is data-gated and never committed)
- [x] Confirm zero contract changes from M1–M3; if any, raise ADR. (the Z80 fit icpu/ichip/ibus and the factory/tier model unchanged — the separate I/O space is handled with injected port callbacks, no contract change. No ADR needed)

---

## M7 — Scripting (Lua + Python)

- [ ] FetchContent integration for Lua 5.4.
- [ ] FetchContent integration for sol2.
- [ ] Build `instrumentation::scripting_lua` with sandboxed VM.
- [ ] Expose introspection API to Lua.
- [ ] Add Lua console panel to dev frontend.
- [ ] Build `mnemos-py` Python package over wire protocol.
- [ ] Publish reference asset-extraction Lua script.
- [ ] Publish reference asset-extraction Python script.
- [ ] Documentation: `docs/scripting/lua-api.md`, `docs/scripting/python-api.md`.

---

## M8 — Sega Genesis

**Status:** Started. The 68000 CPU is being ported from the Emu reference (ADR 0006)
phase by phase, each a tested green commit: **phase 1 (done)** is the functional core
— chip contract (icpu, factory "motorola.68000"), the full register model (D0-7/A0-7/
PC/SR/USP/SSP, 68000 CCR layout), big-endian 24-bit bus access over ibus, all 14
addressing modes, the MOVE/MOVEA/MOVEQ family with the N/Z/V/C flag model, and 4-clock-
per-bus-cycle timing; instruction-stepped with a catch-up tick(). Remaining phases:
arithmetic (ADD/SUB/CMP/MUL/DIV), logical/shift/bit, control flow + the exception
framework (the cycle-accurate prefetch pipeline + address/bus errors + traps +
interrupts), BCD/misc, then the Emu 743-check conformance vectors.

- [~] Implement `chips::cpu::m68000` (full, passes available test suites). (phase 1:
  functional core + 14 EA modes + MOVE family + flags + state, 11 tests. phase 2:
  integer arithmetic — ADD/ADDA/ADDX, SUB/SUBA/SUBX, CMP/CMPA/CMPM, ADDQ/SUBQ,
  ADDI/SUBI/CMPI, MULU/MULS, NEG/NEGX/CLR/EXT/TST, with the full X/V/C flag model
  (flags_add/sub/cmp/addx/subx); 13 more tests (24 total). phase 3: logical/shift/
  bit — AND/OR/EOR/NOT + ANDI/ORI/EORI(+CCR) + BTST/BCHG/BCLR/BSET (3a), then the
  full shift/rotate family ASL/ASR/LSL/LSR/ROL/ROR/ROXL/ROXR in register, immediate
  and memory forms (3b), 13 more tests (37 total). Later phases: control flow + the
  exception framework (which also brings DIVU/DIVS, CHK, TRAPV, MOVE-to/from-SR),
  BCD/misc (ABCD/SBCD/MOVEM/LEA/etc.), cycle-accurate prefetch, then the Emu
  conformance vectors). phase 4a: control flow + the exception core — Bcc/BRA/BSR,
  DBcc/Scc, JMP/JSR/RTS/RTR/RTE, TRAP/TRAPV, LINK/UNLK, MOVE-USP, STOP/RESET
  (privileged), set_supervisor/write_sr + USP/SSP banking, the 6-byte exception frame
  + autovectored 7-level interrupt dispatch + trace + privilege violations; 12 more
  tests (46 total). phase 4b: the trapping arithmetic DIVU/DIVS (with the data-
  dependent cycle model + the divide-by-zero trap) and CHK, plus MOVE-to/from-SR/CCR
  and the ORI/ANDI/EORI-to-SR word forms (all privilege-gated); 7 more tests (51
  total). phase 5: BCD + misc — ABCD/SBCD/NBCD (shared packed-BCD add/sub helpers),
  SWAP, EXG (Dn/An/Dn-An), PEA, LEA, TAS, and MOVEM (register-list transfer, both
  directions incl. -(An)/(An)+ with the predec mask order); 9 more tests (58 total).
  phase 6: MOVEP (Dn<->(d16,An) byte-scatter) + BTST Dn,#imm — completing the
  instruction set; 3 more tests (60 total). The 68000 now decodes every opcode and is
  FUNCTIONALLY COMPLETE. The only remaining work is accuracy, not coverage: the
  cycle-accurate two-word prefetch pipeline + address/bus-error group-0 frames, which
  in turn unlock the prefetch/cycle-exact Emu / public per-instruction conformance vectors (the
  functional core's PC is the logical next-instruction address, not the prefetch-ahead
  value those vectors check, so that harness waits on the prefetch refactor))
- [x] Implement `chips::audio::ym2612`. (DONE in two phases.
  Phase 1 — the control plane: chip contract (iaudio_synth, factory "yamaha.ym2612"),
  the full $20-$B6 register file decoded into the 6-channel/4-operator parameter state
  (the S1,S3,S2,S4 slot remap, the A4/A0 frequency-latch protocol, the channel-3
  per-operator freq mode, key-on/off, feedback/algorithm, stereo + LFO sensitivity),
  the two timers (Timer A 10-bit at 1008 master clocks, Timer B 8-bit at 16128, overflow
  flags + status + IRQ + the $27 reset/run/enable bits + CSM force-key), the channel-6
  DAC, the analog-output low-pass, and save/load.
  Phase 2 — the FM synthesis core: the phase generator (fnum/block + hardware DT1 detune
  + multiply, LFO vibrato), the per-operator envelope generator (attack/decay/sustain/
  release with key-scaling + SSG-EG, hardware eg_pattern/eg_rate_select tables, /3 EG
  clock), the 8 FM algorithms with operator-0 feedback, LFO tremolo, the log-sine/exp
  output pipeline, per-channel mixing with the channel-6 DAC override, hyperbolic
  soft-clip, and the stereo low-pass; step()/update() render audio. Built from the
  canonical published reverse-engineering / open-source OPN2 model. 18 tests / 61 assertions, green;
  full ctest suite green (32 tests). Deferred accuracy: cycle-exact per-operator update
  ordering vs the conformance vectors awaits a real Genesis ROM to validate against)
- [ ] Verify reuse of `chips::audio::sn76489` (Genesis PSG).
- [ ] Verify reuse of `chips::cpu::z80` (Genesis sound CPU).
- [x] Implement `chips::video::genesis_vdp` (Genesis VDP, Sega 315-5313). DONE in two
  phases. Phase 2 added the pixel renderer: 8x8 4bpp pattern decode (incl. interlace-2
  8x16), Scroll A/B planes (full/cell/line H-scroll + full/2-cell V-scroll, plane
  sizes, flip/priority/palette per cell), the Window layer, 80 sprites (link-list
  traversal, per-line sprite + pixel limits with overflow flag, X=0 masking, collision,
  column-major tile layout), the plane/sprite priority composite, shadow/highlight, and
  CRAM->0x00RRGGBB output into the framebuffer (display-disabled backdrop fill, blank-
  left-8, H32 right margin); 15 tests / 38 assertions (plane tile, backdrop, sprite),
  full ctest green (33). Phase 1 was the control plane: ivideo+immio contract, factory "sega.315_5313", 64KB VRAM
  + 64-entry CRAM (9-bit, $0EEE mask) + 40-entry VSRAM (11-bit) with the byte-swap/odd-
  address quirks, all 24 registers with the mode-4 upper-register lockout, the two-word
  command-port protocol (CD code + address, auto-increment, read-prefetch buffer with
  CRAM/VSRAM/byte open-bus mixing), the DMA engine (68K->VRAM transfer via a host
  read callback, VRAM fill, VRAM copy with the source-bank-wrap rules), the H/V counter
  readback (hardware cycle2hc32/cycle2hc40 tables ported as data), the status register,
  and the scanline-accurate timing + interrupt engine (tick()-driven, 3420 master
  clocks/line, VINT6/HINT4/EXT2 with the H-int line counter, status-read ack, level
  callback); 12 tests / 32 assertions, full ctest green (33 tests). Deferred to a later
  accuracy pass (validatable only against ROM/timing suites): the cycle-exact FIFO
  service timing + open-bus mix-FIFO, active-display DMA pacing, mid-line CRAM/VSRAM
  pending-write deferral, and sub-line H-counter sampling. Phase 2 = the pixel renderer
  (Scroll A/B, Window, 80 sprites with link traversal + per-line limits, priority,
  shadow/highlight) into the framebuffer)
- [ ] Implement Genesis cartridge mappers (SSF2, EEPROM-backed, etc.).
- [~] Author `manifests/genesis/` (NTSC + PAL). (phase 1 DONE — the 68000 side:
  assemble_genesis wires the full memory map (ROM $000000-$3FFFFF, Z80 RAM $A00000,
  YM2612 $A04000, controller I/O + version register $A10000, Z80 bus-req/reset
  $A11100/$A11200, the VDP $C00000 with 16-bit even/odd byte-pair coalescing for the
  two-word command protocol, the SN76489 PSG at $C00011, and 64KB work RAM mirrored
  $E00000-$FFFFFF), the VDP V/H-blank IRQ into the 68000 IPL, VDP DMA reading big-endian
  words from 68K space, and boots from the ROM reset vectors; NTSC/PAL region select.
  5 tests / 16 assertions (boot vectors, a hand-assembled boot program exercising work
  RAM + the coalesced VDP path + the VINT handler end-to-end, A-bank device routing,
  DMA-from-68K, PAL); full ctest green (34).
  Phase 2 DONE — the Z80 sound subsystem: its own little-endian bus (Z80 RAM mirrored,
  the YM2612 at $4000, the PSG at $7F11, the $6000 9-bit shift bank register, and the
  $8000-$FFFF banked window into 68K space), BUSREQ/RESET arbitration ($A11100/$A11200)
  with the Z80 reset on the falling edge and the bus-grant readback, and a gated_chip
  adapter that advances the Z80 in the scheduler only while it owns its bus; 8 tests /
  29 assertions (arbitration gating, the 68K bank window, Z80-bus device routing). Phase
  3: combine YM2612 + PSG into mixed stereo audio output, and the 3/6-button controller
  protocol)
- [~] Golden frame tests for known commercial Genesis ROMs. (tests/golden/genesis_boot_test
  + run-data-gated-tests wiring DONE — MNEMOS_GENESIS_ROM, data-gated/skips in CI. Surfaced
  real boot-divergence bugs vs the working Emu reference; FIXED the first: the 68000 had no
  interrupt-acknowledge path, so VDP VINT was only cleared by a status read and games whose
  V-blank handler relies on the IACK to clear it re-entered forever. Added m68000
  set_irq_ack_callback (invoked in process_interrupt) + genesis_vdp::acknowledge_irq, wired in
  the manifest. Result: a previously-blank title now boots to a rendered frame; another went
  0 -> 13K VRAM words. REMAINING divergences to chase against the reference: one title loads
  tiles but no CRAM/palette (still blank); another hangs very early (pc=$0588, reset SR) -- more
  68000 instruction bugs, now findable via the conformance harness below)
- [~] 68000 conformance: tests/.../m68000_singlestep_test against the public per-instruction
  680x0 corpus (MNEMOS_M68000_TESTS_DIR, data-gated/skips in CI). Instruction-stepped relaxed
  compare (D/A/USP/SSP/SR/RAM, not the prefetch-coupled PC/queue/cycles); filters group-0
  (address/bus-error) cases by detecting a vector to the $08/$0C handler (unit tests cover
  functional vector-2/3 frames; concrete BERR maps and prefetch-exact microstate remain open).
  IMPORTANT: the corpus places the opcode AT pc (prefetch[0]@pc,
  prefetch[1]@pc+2, further extension words in ram at pc+4+), NOT at pc-4 -- an initial harness
  off-by-4 made every stream-operand instruction read garbage extension words. After fixing
  that, the corpus runs clean. Final run: 103 of 124 instruction files (the largest 21 keep
  timing out on the corpus download) pass with no cap, ~700K+ tests, zero failures (plus
  2 documented corpus anomalies whitelisted -- the two `e502 [ASL.b Q, D2]`
  tests whose expected final mutates the high 24 bits of D2, impossible for a byte shift on
  a Dn). **The m68000 core is essentially fully validated.** The Genesis game-boot divergences
  (one title's palette, another's early hang) therefore live in the VDP / manifest, not
  the CPU -- the next hunt to compare against the Emu reference's Genesis VDP
  and m68k systems. FIRST VDP FIX: the VDP was asserting dma_busy the moment a FILL DMA
  command was decoded, but real hardware only asserts busy once the data-port write actually
  starts the fill. Some titles defensively poll dma_busy after the command and
  deadlocked. Fix: keep dma_fill_pending=true but leave dma_busy=false on the FILL trigger.
  **Local Genesis boot rate jumped from 1/10 to 7/10 titles** in a quick sweep.
  TWO MORE SYSTEMIC FIXES landed against the Emu reference: (a) VDP vblank-state
  callback wired to the Z80 INT line (matches sync_z80_irq_line); (b) optional 68000
  TAS write-back callback; the manifest installs a no-op for the Sega Genesis bus
  controller's TAS-write quirk (matches m68k_tas_cb). **Boot rate now ~88%** in
  a random 50-title sweep (seed 42). 36/36 ctest still green. Remaining
  hangs each have distinct patterns -- individual title / Z80-sync bugs. The 98% target
  needs more systemic fixes: candidates are active-display DMA pacing, FIFO timing
  accuracy, mid-line CRAM/VSRAM deferral. NOTE: the local ROM library layout was
  reorganised mid-2026 (paths kept in the git-ignored scripts/local-roms.ps1, never here).
  Re-sweep against the new library: ~72% at 180 frames, ~82% at 400 frames (several hangs
  were just slow boots). Remaining hangs each have distinct stuck states; one branched into
  the cartridge header at pc=0x194. 36/36 ctest still green)
- [ ] Validate dual-CPU scheduling correctness.

---

## M9 — Multiplatform Parity Audit

- [ ] Full clean Linux build in a fresh container, no Windows host involvement.
- [ ] Linux performance baseline recorded.
- [ ] Linux input (evdev / SDL_gamepad) verified across at least 3 gamepad models.
- [ ] Linux audio path verified.
- [ ] Sanitizer builds (ASan, UBSan) green on full system suite.
- [ ] Populate `BUILDING.md` for both platforms.
- [ ] Populate `CONTRIBUTING.md`.

---

## M10 — Netplay Foundations

- [ ] Confirm M3 input subsystem meets rollback requirements; revise if not.
- [ ] Add cross-client state hash introspection capability.
- [ ] Implement rollback engine (input prediction, mispredict detect, resimulate).
- [ ] Implement lockstep fallback transport.
- [ ] Implement local lobby + direct-connect.
- [ ] Implement netplay-aware input mapping (player slot assignment).
- [ ] Smoke test: two SMS instances on same LAN, 2-player ROM, 10-minute session, no desync.
- [ ] ADR `docs/adr/00NN-netplay-architecture.md`.

---

## Cross-Cutting Backlog (Not Tied to a Single Milestone)

### Governance
- [ ] Implement `naming_validator` tool (M1 or later).
- [ ] Implement `tier_dependency_validator` tool (verify CMake graph matches TDS §4).
- [ ] Wire both validators into CI.
- [ ] Author governance ADRs as new patterns emerge.

### Documentation
- [x] Per-chip `NOTES.md` template adopted from the first chip implementation onward. (this commit; `src/chips/cpu/m6510/NOTES.md`)
- [ ] Per-system `NOTES.md` template adopted from C64 onward.
- [x] `docs/architecture/` index page with reading order.
- [x] `docs/adr/` index page with chronological list.

### Tooling
- [ ] ROM hashing utility (`tools/rom_hasher`) for manifest authoring.
- [ ] Manifest validator standalone CLI (`tools/manifest_validator`).
- [ ] Save-state inspector (`tools/state_inspect`) for debugging.

### Performance (post-M3)
- [ ] Microbenchmark harness in `tests/bench/`.
- [ ] Per-chip cycle-cost benchmark.
- [ ] System-level frames-per-second benchmark on reference hardware.

---

**End of Mnemos Todos v0.1.**
