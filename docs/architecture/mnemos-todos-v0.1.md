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
- [x] Define `i_chip` interface (TDS §8.2). (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define specialized interfaces: `i_cpu`, `i_audio_synth`, `i_video`, `i_bus_controller`, `i_storage`, `i_mapper`, `i_peripheral`. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `i_chip_introspection` interface (forward-decl OK; full def in M4). (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
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
- [ ] Implement save / load state.
- [x] Implement introspection (register snapshot, PC, cycle counter, instruction event tap). (this commit; register_snapshot()/elapsed_cycles()/cpu_registers() on m6510. The instruction-event tap rides with the M4 instrumentation interface, which is minimal in M1 by design)
- [x] Register factory under `"mos.6510"`. (72e4a6e; CI run 26320527793 green across clang-format and all 6 build/test jobs)

### Test infrastructure for 6502 family
- [x] Vendor or fetch Klaus 2M65 functional test ROM with provenance documented. (superseded by the Tom Harte per-cycle corpus per ADR 0006 — a stronger conformance gate; provenance in src/chips/cpu/m6510/tests/README.md)
- [x] Vendor or fetch decimal mode test. (covered by Tom Harte: decimal ADC/SBC/ARR validated across the corpus)
- [x] Vendor or fetch undocumented opcode test suite. (covered by Tom Harte: stable illegals validated; unstable/JAM out of scope per NOTES.md)
- [x] Author `m6510_conformance_test` integrating all of the above. (daf7aba; `m6510_tomharte_test`, ~2.4M vectors pass locally. Data-gated; kept local-only per decision — corpus never committed, skips in CI)
- [x] Author per-opcode microtests (cycle count, flag updates) for at least the trickiest 20 opcodes. (the m6510_test suite covers page-cross, RMW dummy-write, indirect-JMP bug, decimal, branch timing, interrupts, illegals)

### Acceptance
- [~] `m6510_conformance_test` passes on all four matrix combinations. Validated locally against the full Tom Harte corpus across Win/Linux × Debug/Release behavior; CI execution kept local-only per ADR 0006 (corpus never committed). CI runs it as a skipped, data-gated test.
- [x] Zero warnings, zero sanitizer hits when run under ASan+UBSan on Linux Clang. (d328480; CI run 26324986992 `linux-clang-asan` green)
- [x] ADR `docs/adr/0004-chip-contract.md` records the contract decisions made during this milestone. (0c... see docs/adr/0004-chip-contract.md)

---

## M2 — Chip Library Expansion (C64 Set)

**Status:** Substantially complete. All five C64 chips (6510, VIC-II, SID, CIA, PLA)
are implemented and unit-tested; the VIC-II scanline renderer covers standard +
multicolour text + border (bitmap/ECM and the sprite engine remain). Per-chip
save/load landed with the M3 save-state format. Remaining: SID register-trace
validation against a known tune, and the deferred VIC graphics modes/sprites.

### VIC-II 6569
- [x] Create `src/chips/video/vic_ii_6569/` library target. (a013df0; ported from Emu per ADR 0006; CI run 26325382940 green across all jobs)
- [x] Implement raster engine (PAL: 312 lines × 63 cycles). (a013df0; beam tracker + per-cycle video-matrix counters, PAL/NTSC geometry; CI run 26325382940)
- [~] Implement all graphics modes (standard text, multicolor text, standard bitmap, multicolor bitmap, extended color text). (mode DECODE done in a013df0; scanline renderer now emits standard + multicolour text into a per-frame framebuffer fetched from attached RAM/char-ROM/colour-RAM; standard/multicolour bitmap + ECM + cycle-exact beam alignment still pending)
- [ ] Implement sprite engine (8 sprites, multicolor, expansion, sprite-sprite/sprite-data collisions). (sprite X/Y latches + collision read-clear done; fetch/compositor/priority/collisions net-new, pending)
- [~] Implement border generation (open borders trick must work). (display-window vs border decided per pixel from DEN latch + CSEL/RSEL geometry; the open-border / sprite-in-border tricks need cycle-exact beam state and are pending)
- [x] Implement raster IRQ generation. (a013df0; edge-latched raster compare + mask + master + write-1 ack + light-pen source; CI run 26325382940)
- [x] Implement badline cycle stealing. (a013df0; bad-line condition + BA-low + CPU-read-stall windows; CI run 26325382940)
- [x] Implement MMIO register set ($D000–$D3FF mirrored). (a013df0; full $D000-$D02E path, $D02F-$D03F read $FF, 64-byte mirror; CI run 26325382940)
- [ ] Save / load state. (deferred to M3 with the runtime save-state format, as for the m6510)
- [x] Introspection: register snapshot, current raster line, current cycle, sprite states. (a013df0; register_snapshot + raster_y/raster_x; full sprite-engine states arrive with the sprite compositor)

### SID 6581
- [x] Create `src/chips/audio/sid_6581/` library target. (a7dc3f8; ported from Emu per ADR 0006; CI run 26387148559 green across all jobs)
- [x] Implement register interface ($D400–$D7FF mirrored). (a7dc3f8; 32-byte alias, write-only/read-only classes)
- [x] Implement three voices with ADSR. (a7dc3f8; datasheet rate table + exponential decay/release divider, gate re-trigger)
- [x] Implement waveform generators (triangle, sawtooth, pulse, noise, combined). (a7dc3f8; 23-bit noise LFSR, combined-AND, 8580 partial-restore)
- [x] Implement filter (resonance + cutoff; v0.1 acceptable as "audible and stable", quality refinement is its own task). (a7dc3f8; state-variable LP/BP/HP, per-variant range)
- [x] Implement ring modulation and sync. (a7dc3f8)
- [x] Implement read-back of OSC3 and ENV3. (a7dc3f8)
- [ ] Save / load state. (deferred to M3 with the runtime save-state format)
- [x] Introspection: register snapshot, voice states, filter state. (a7dc3f8; envelope/volume snapshot + waveform_output/voice_phase/envelope accessors)
- [x] Reference: at least one published SID emulation note set documented in `src/chips/audio/sid_6581/NOTES.md`. (a7dc3f8; MOS 6581 datasheet + Yannes interview per Emu PROVENANCE)

### CIA 6526
- [x] Create `src/chips/bus_controller/cia_6526/` library target. (91172fb; ported from Emu per ADR 0006; CI run 26386882167 green across all jobs)
- [x] Implement two parallel ports (A and B). (91172fb; DDR + live host callbacks + PB6/PB7 timer output)
- [x] Implement two timers (A and B) with all modes (one-shot, continuous, cascaded). (91172fb; incl. force-load + start-delay pipelines, φ2/CNT/TA-cascade input modes)
- [x] Implement TOD clock. (91172fb; 50/60 Hz divider, BCD with AM/PM, alarm, latched read, write freeze)
- [x] Implement serial shift register. (91172fb; input + output modes with SDR-complete IRQ)
- [x] Implement IRQ generation and mask. (91172fb; NMOS edge-triggered IR flip-flop + 1-φ2 pin delay + read-clear)
- [ ] Save / load state. (deferred to M3 with the runtime save-state format)
- [x] Unit tests covering timer mode transitions. (91172fb; continuous/one-shot/cascade + NMOS imr edge + TOD/alarm/SDR; full Lorenz-suite validation is M3 system integration)

### C64 PLA (memory banking)
- [x] Create `src/chips/mapper/c64_pla/` library target. (76acf1f; ported from Emu per ADR 0006; CI run 26386496106 green across all jobs)
- [x] Implement LORAM/HIRAM/CHAREN/GAME/EXROM decoding to the 14-state banking table. (76acf1f; decode_cpu_address/decode_vic_address across standard/8K/16K/ultimax; CI run 26386496106)
- [x] Wire to bus region overlay control. (src/manifests/c64 — assemble_c64 maps RAM/BASIC/KERNAL/CHARGEN/I-O overlays whose active predicates call decode_cpu_address with the live 6510 $01 port; c64_system_test exercises every bank state)

### Acceptance
- [~] Each chip's unit tests pass in CI. (VIC-II, SID, CIA, PLA all green; M2 not fully complete — VIC-II rendering/sprites/border still pending)
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
keyboard/joystick/paddle input (B2) and ADR 0005 (scheduler strategy). The only
remaining item is the first golden boot test, data-gated on local C64 ROMs — the
CLI + framebuffer-hash pipeline is ready.

### Topology library
- [x] Create `src/topology/` library target `mnemos::topology`. (7e62c0d; tier-3 library implementing chips::i_bus; CI run 26387396049 green)
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
- [x] Implement manifest-to-component-graph builder (instantiate chips by ID via the factory registry, wire buses). (9da1028; build_system creates chips via create_chip, builds topology buses, allocates RAM, binds MMIO via i_mmio, attaches CPUs; CI run 26388745058)
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
gaps the Emu review surfaced (Emu = `C:\Users\mkrol\source\repos\Emu`).
- [x] Route VIC + CIA1 /IRQ into the 6510 /IRQ and CIA2 into /NMI. (B1; assemble_c64 ORs vic.irq_asserted()|cia1 via edge callbacks into set_irq_line, CIA2 -> set_nmi_line; the single biggest correctness blocker — without it no IRQ-driven program runs)
- [x] Track the VIC 16K bank from CIA2 port A at runtime. (B3; CIA2 write_port_a -> vic.set_bank((~port_a_pins())&3))
- [x] Keyboard matrix + joystick wiring (CIA1 PRA/PRB callbacks) and paddle/POT mux to SID. (B2; c64_input models the 8x8 matrix + both joysticks + both paddle pairs, wired into CIA1 PRA/PRB in assemble_c64; the paddle mux (CIA1 PRA bit7->port1, bit6->port2) routes the selected pair to SID POTX/POTY ($D419/$D41A). Matrix scan, joystick overlay, and paddle mux all unit + integration tested. Feeding the frame-tagged input_buffer / a frontend into c64_input is a runner/frontend concern, not chip wiring)
- [ ] VIC floating-bus / open-bus read semantics (unmapped I/O returns last VIC fetch). (B4)
- [x] Cartridge support: `.crt` loader + generic 8K/16K/Ultimax + Ocean/Magic Desk + EasyFlash; drive `/GAME`//EXROM into the PLA. (B5; chips::mapper::c64_cartridge parses .crt + banks ROML/ROMH + I/O bank-switching; assemble_c64 maps ROML $8000 / ROMH $A000+$E000 / cart I/O $DE00 gated by the PLA, and feeds the cart's /GAME//EXROM into the decode; CLI --cart. The cart I/O-2 open-bus latch is the separate B13)
- [x] Disk: IEC serial bus + synthetic 1541 (devices 8-11) + `.d64` reader for protocol-level LOAD. (B6; chips::iec_bus + chips::storage::c1541::{d64_image, synthetic_drive} + CIA2 IEC wiring in assemble_c64 + CLI --disk. Command/serving logic unit-tested; the bit-level handshake is ROM-gated like the golden boot)
- [ ] Datasette: 1530 `.tap` v0/v1 pulse playback -> CIA1 /FLAG. (B7; small, self-contained)
- [ ] REU (1700/1764/1750) `$DF00` DMA controller. (B8)
- [x] Full cycle-accurate 1541 (6502 + 2x 6522 VIA + GCR) and the MOS 6522 VIA chip. (B9; via_6522 + chips::storage::c1541::{gcr, disk_bind, full_drive} — drive 6502 (port-disabled m6510) + 2x VIA + 2K RAM + 16K DOS ROM + GCR head/stepper/SYNC + IEC auto-ATN-ack. Memory map / VIA wiring / mechanism unit-tested with a synthetic ROM; real DOS ROM is data-gated and the GCR read path tracks Emu's unfinished state)
- [x] System-level SID variant / NTSC region / dual-SID selection + an NTSC manifest. (B10; assemble_c64 takes a c64_config — region sets VIC revision + phi2 + mains TOD, sid_variant picks 6581/8580, dual_sid maps a second SID at $D420 (priority overlay over the SID mirror). c64.ntsc.toml added; CLI derives region from the manifest id and takes --sid/--dual-sid. Verified PAL vs NTSC produce different frame hashes)
- [x] 6510 unstable illegal opcodes (SHA/SHX/SHY/TAS/LAS/ANE/LXA) + `$01` bit 6/7 floating-gate fade. (B11; decoded + executed deterministically — ANE/LXA with a fixed magic ($EE), SHA/SHX/SHY/TAS store source & (high+1), TAS sets SP=A&X, LAS = mem & SP; the $01 bits 6,7 fade to 0 after switching to input. Page-cross address corruption not modelled. Unit-tested)
- [ ] RS-232 / userport modem (CIA2 PA2 TXD + userport, Emu `c64_modem.c`). (B12; genuine userport peripheral, surfaced by the re-review; not covered by B1-B11. Niche — low priority)
- [ ] Cartridge I/O-2 ($DF00) open-bus "last byte" latch. (B13; distinct from B4's VIC floating bus — the cartridge-port stale byte fastloaders/protection probe; lands with B5 cartridge support)

### Runtime library
- [x] Create `src/runtime/` library target `mnemos::runtime`. (compiled tier-5 static lib)
- [x] Implement master clock with divider table. (scheduler tracks the master cycle; each chip carries a master->chip divider)
- [x] Implement fixed-divider scheduler dispatching per-chip ticks. (scheduler dispatches tick() per cycle in chip order, with a lockstep fast path when all dividers are 1; TDS §11.2)
- [x] Implement frame-tagged input buffer. (input_buffer keeps events sorted by frame for deterministic replay; CIA1 keyboard/joystick wiring is follow-up)
- [x] Implement frame boundary detection and signaling. (run_frame / run_frames advance until the designated i_video frame_index increments)
- [x] Implement save state with header + per-chip chunks (TDS §15). (runtime::write_save_state / read_save_state: MNMS header + per-chip + per-memory chunks; per-chip save_state/load_state implemented for all 5 C64 chips; unknown chunks skipped for forward-compat)
- [x] FetchContent integration for `zstd`. (ADR 0008; pinned v1.5.6, SOURCE_SUBDIR build/cmake, libzstd_static linked PRIVATE into tier-5 runtime)
- [x] Implement save state compression (zstd) and decompression. (level-3 frame around the chunk body)
- [x] Implement CRC32 trailing checksum. (foundation::crc32 over header+compressed body, verified on load)
- [x] Implement rewind ring (configurable depth, default 600 frames). (runtime::rewind_ring; full states, at_or_before seek, oldest-evict)
- [x] Unit tests for scheduler dispatch, save/load roundtrip, rewind. (scheduler dispatch/dividers/frames + input buffer + save_state round-trip/corruption/mismatch/unknown-chunk + rewind seek/evict/clear; per-chip save/load round-trips in each chip's test)

### Headless runtime CLI
- [x] Create `tools/mnemos_runtime_cli/` executable target. (tier-8 `mnemos_runtime_cli` + shared `mnemos_runtime_cli_core` lib so tests reuse the logic)
- [x] CLI options: `--manifest`, `--rom-dir`, `--frames`, `--dump-hash`, `--save`, `--load`, `--input-log`. (all parsed; `--save` writes a save state after the run, `--load` restores before it; `--input-log` still deferred)
- [x] Outputs framebuffer hash (SHA-256 of RGBA bytes) per frame or at end. (at end via `--dump-hash`; pixels serialised R,G,B,A for a cross-platform-stable hash. Loads the manifest, reads/verifies ROMs from `--rom-dir` — placeholder sha256 -> unverified warning — assembles the C64, runs `--frames`, hashes. Verified deterministic end-to-end with synthetic ROMs.)

### First golden test
- [ ] Author `tests/golden/c64_basic_boot_test.cpp`. (blocked on local C64 ROMs; the CLI + hash pipeline is ready and reproducible, so this is data-gated only)
- [ ] Boot C64 manifest with no cartridge, run 600 frames (~12 s @ 50 Hz PAL).
- [ ] Hash framebuffer at frame 600.
- [ ] Commit the expected hash.
- [ ] CI runs this on every PR.

### Acceptance
- [ ] `mnemos_runtime_cli load c64.pal --frames 600 --dump-hash` produces an identical hash on Win+Linux, Debug+Release.
- [x] Save → load → continue produces an identical hash trajectory from the load point onward. (verified via the CLI: load@frame 3 + 3 frames matches a straight 6-frame run; master cycle restored)
- [x] ADR `docs/adr/0005-scheduler-strategy.md` records the fixed-divider choice and its limitations. (records the fixed-divider master-clock model, lockstep fast path, rendering-agnostic frame detection, and the v0.1 limitations: no sub-cycle bus arbitration / static ratios only)

---

## M4 — Instrumentation API and Developer Frontend MVP

(Scoping-level items; expand at milestone start.)

- [ ] Promote `i_chip_introspection` from forward decl to full interface.
- [ ] Implement `i_runtime_introspection`.
- [ ] Implement breakpoint engine (PC, memory R/W, conditional).
- [ ] Implement watch engine.
- [ ] Implement event subscription with filters.
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

- [ ] Implement `chips::cpu::z80` (full undocumented behavior; passes ZEXALL).
- [ ] Implement `chips::audio::sn76489`.
- [ ] Implement `chips::video::sms_vdp`.
- [ ] Implement SMS mapper (Sega mapper, Codemasters mapper).
- [ ] Author `manifests/sms/` (NTSC + PAL).
- [ ] Golden frame tests for known commercial SMS ROMs.
- [ ] Confirm zero contract changes from M1–M3; if any, raise ADR.

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

- [ ] Implement `chips::cpu::m68000` (full, passes available test suites).
- [ ] Implement `chips::audio::ym2612`.
- [ ] Verify reuse of `chips::audio::sn76489` (Genesis PSG).
- [ ] Verify reuse of `chips::cpu::z80` (Genesis sound CPU).
- [ ] Implement `chips::video::vdp_315_5313` (Genesis VDP).
- [ ] Implement Genesis cartridge mappers (SSF2, EEPROM-backed, etc.).
- [ ] Author `manifests/genesis/` (NTSC + PAL).
- [ ] Golden frame tests for known commercial Genesis ROMs.
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
