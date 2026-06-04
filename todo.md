# TODO — APM-Style Tracer Sidecar (self-contained capability)

Goal: a **separate tracer executable** that loads the emulator as a **plugin**, gives it a
**custom tagged memory allocator**, and observes/records execution from the **outside as a
sidecar** — like Dynatrace/Datadog APM. The emulator owns **zero** logging/observer/DB logic;
it just runs. Tracing is a standalone product in its own subfolder, linked into both our engine
*and* the reference core via a C ABI, so their traces are directly diffable.

Budget: up to ~10 GB of trace per run is acceptable. Store columnar (Parquet) → query with DuckDB.

Driving problem: Genesis boot-frame parity stuck at ~78% byte-exact ("C"); dominant remaining
class is a sub-frame raster-phase fork from one root cause. This toolkit makes root-causing a
*bisect query* instead of an hours-long re-run-and-grep loop.

---

## Architecture (the model of record)

```
tracer.exe  (the HOST / bootstrap — our process)
  ├─ tagged memory allocator  (specialized for tagging/debugging/traceability)
  ├─ loads emulator.dll as a plugin (C-ABI factory)
  ├─ hands the emulator the allocator  → all chip banks allocated + TAGGED by us
  ├─ observer / watchers / perf counters / emit  (the sidecar)
  └─ Parquet sink → DuckDB → lockstep diff + auto-bisect

emulator.dll  (PLUGIN — stays pristine)
  ├─ exports a C-ABI factory + run/step entry points
  ├─ sources every memory bank from the injected imemory_allocator (page-aligned)
  └─ NO knowledge of logging, observers, DB, or the tracer
```

### The one law of physics we design around
A guest write is just `host_bank[addr] = value` — nothing outside the process sees it per-write
for free. Three observation modes, each with a hard limit:
1. **Boundary diff** of tagged regions (frame/scanline/trigger): zero-cost, zero-seam, but
   coarse (what changed, not the sequence/PC).
2. **OS page-protection** on tagged pages (VirtualProtect + VEH): precise + zero-seam, but a
   fault is ~µs — perfect for a *handful* of watched addresses, fatal if applied to all banks.
3. **One in-emulator seam** (single-step entry / accessor hook): the only way to get the full
   per-instruction *opcode-execution sequence* cheaply (fetch can't be page-faulted).

### What that buys per stream
| Stream | Seam-free? | Mechanism |
|---|---|---|
| Tagged whole-memory snapshots + per-frame/scanline value diffs | ✅ | allocator owns banks; tracer diffs by tag |
| Perf counters, emit/event API, sidecar, storage/query/diff | ✅ | all in tracer |
| Cycle/PC-precise watchpoints on a few addresses | ✅ | page-protect those tagged pages; read guest PC from the tagged CPU-state bank in the fault handler |
| Full per-instruction opcode *sequence* | ⚠️ 1 seam | single-step entry the plugin exports |

### Universal index on every record
`(frame, master_cycle, scanline, hcounter, global_inst#)`. Align on **master_cycle** (immune to
the reference's instruction coalescing) — never raw instruction index, never raw frame number
(absorbs the +1/+2 harness offset).

---

## Phase 0 — Make the emulator pluginnable + allocator-fed (the only engine-side work)

- [ ] Define `imemory_allocator` interface (alloc tagged, page-aligned banks; free; query tag).
- [ ] Move chip memory banks off inline `std::array` members onto the injected allocator
      (e.g. `genesis_system` work_ram/z80_ram/CPU-state/VRAM/CRAM/VSRAM → allocator-provided,
      page-aligned, tagged `(chip, bank, guest_base, size)`).
- [ ] Export a C-ABI plugin surface: `create(allocator)`, `run_frame()`, `step_instruction()`,
      `load_rom()`, state query. Build the engine as `emulator.dll`.
- [ ] Default in-process allocator (malloc-backed) so standalone runs/tests are unchanged.
- [ ] Parity guard: both assemble paths + the existing sweep harness still byte-identical.

## Phase 1 — Tracer host + tagged allocator (self-contained module)

- [ ] `apm/` module: own CMake target, own tests, own README. No engine internals included.
- [~] Tagged allocator: per-bank tags + page-aligned arenas + reverse address->tag lookup DONE
      (`apm/memory/tagged_allocator` + `bank_registry`, implementing the `imemory_allocator`
      contract; slice #2, 6 tests pass). Pending: guard-band option, alloc/free log.
- [x] Plugin C ABI (`apm/abi/apm_plugin_abi.h`) + genesis binding DLL (`apm/bindings/genesis`,
      `mnemos_genesis.dll`) wrapping `build_genesis_runtime` behind create/load_rom/run_frame/
      read_register/get_bank — ZERO engine change. Binding test (synthetic ROM) passes (slice #3).
- [x] Bootstrap `mnemos_tracer.exe` (`apm/host`): `LoadLibrary(mnemos_genesis.dll)`, drive
      run_frame, arm a page-protection watchpoint on a bank address, read guest PC via the ABI.
      Links only abi + memory; the engine arrives at runtime. (slice #3)
- [ ] Perf counters (per-chip cycles, stalls, instr counts, fault counts, trace bytes/s) + an
      emit/event API.

## Phase 2 — Observer & watchers (the sidecar)

- [ ] **Boundary observer**: snapshot + diff tagged regions at frame/scanline/trigger → tagged
      change records (chip, bank, addr, old→new, frame, master_cycle).
- [~] **Page-protection watchpoints**: protect selected tagged ranges; VEH handler records the
      write/read, reads **guest PC from the tagged CPU-state bank**, then single-steps + re-protects.
      MECHANISM PROVEN standalone in `apm/memory/page_guard.{hpp,cpp}` (slice #1, 3 tests pass:
      intercept+host-IP, re-arm across repeated writes, sub-page filter). Pending: wire to the
      engine plugin + read guest PC via the ABI instead of host IP.
  - [x] Core engine: VirtualProtect + VEH intercept/recover/re-arm (apm/memory/page_guard).
  - [x] Write-watch wired to the REAL engine via the binding+host: traced Kid Chameleon `$FFF809`
        from outside (writer `pc=$000488` per frame; `pc=$000720` resets it at frame 24). 130
        frames in ~6 s. Guest PC read via the ABI (`read_register(APM_REG_INST)`), not host IP.
  - [ ] **Read-watch** VDP status `$C00004` / HV-counter `$C00008` + the branch taken next
        (the boot-wait-loop poll — root-cause mechanism for the current bug).
  - [x] Page-granularity filter (4 KiB pages; filter unrelated addrs in handler).
- [ ] Value/opcode watchers configurable by tag + address + predicate (trigger capture on hit).

## Phase 3 — Opcode-sequence stream (the one seam)

- [ ] Plugin exposes a single-step entry; tracer drives it to record per-instruction
      `master_cycle, cpu_cycle, pc, opcode, changed regs, cycles_charged, stall_charged`.
- [ ] Bus-access sub-stream `(addr, r/w, size, value, region, wait_cycles)`.
- [ ] Toggleable per window (`frames N..M`, PC range) so the heavy stream isn't always on.

## Phase 4 — Symmetric reference core

- [ ] Wrap the reference core behind the **same C-ABI plugin surface** + the same allocator/
      single-step entry, so it feeds the **same collector → identical schema**.
- [ ] Verify both plugins emit schema-identical Parquet.

## Phase 5 — Store, lockstep diff, auto-bisect (the payoff)

- [ ] Parquet sink + DuckDB ingest. Tables: `instr, bus, mem_write, mem_read, snapshot,
      exception, stall, perf`, keyed by `(engine, run_id, global_inst#, frame, master_cycle)`.
- [ ] Cross-engine join on `master_cycle`: **first cycle where `ours.pc != ref.pc`**.
- [ ] Auto-bisect to the first divergent basic block + the read/branch that caused it.
- [ ] Breakpoint-on-divergence harness: run both plugins, halt at first watched-value mismatch.
- [ ] Deterministic replay / state-load at the divergence for side-by-side single-step.

## Phase 6 — Control flow, snapshots, timing ledger

- [ ] Branch/call/return trace → CFG + dynamic call-tree diff; hot-loop iteration counters.
- [ ] Per-scanline CRAM/VSRAM snapshots (raster-effect class).
- [ ] Exception/IRQ trace (vector, master_cycle, scanline, interrupted PC, SR).
- [ ] Stall/timing ledger (per-frame cycles, stall by type, VBlank-entry cycle).

---

## Build-first order (max leverage)

1. **Phase 0** — pluginnable + allocator-fed engine (unblocks everything; DI, clean refactor).
2. **Phase 1 + Phase 2 page-protection read/write watchpoints** — cracks the current bug seam-free.
3. **Phase 5 store + lockstep diff** — turns divergence-finding into a query.
4. **Phase 3 opcode-sequence** (the one seam) — full per-instruction parity.
5. **Phase 4 reference plugin** — symmetric, directly diffable.
6. **Phase 6** — structural/raster/timing depth.

---

## Current lead (retained for whoever picks this up)

- Cart: **Kid Chameleon**. Aligned compare = `Mnemos f120 ≡ reference f121` (+1 offset).
- Divergent work-RAM: wave table `$FF4E5C-71` (same values, **shifted phase**) + phase counters
  `$FFF7F5`, `$FFF809`.
- Both engines write `$FFF809` **once per frame from the same `pc=$000488`** (a per-frame counter).
- **Mnemos's counter is a constant +8 ahead** (`$00` at M=frame24 vs ref frame32, +8 across the
  whole range; one double-write at frame 24 = the counter's start). The counter is faithful — the
  fork is **when the boot phase that starts it completes**: Mnemos reaches it ~7–8 frames earlier,
  i.e. progresses through early boot faster. Seeds near the frame-6 VRAM-clear region.
- **Next disciplined step (Phase 2 read-watch):** page-protect VDP status, fault on the boot
  wait-loop's poll in both engines, find the read whose returned bit differs + the branch it
  flips — the root cause of the 8-frame lead, hence the wave-table phase shift.

---

# TODO — Chip/Mapper Variant Coverage (ROM support & compatibility)

Scope: the variant work that decides **whether a ROM boots and runs**, for the C64, SMS, and
Genesis. Ordered by compatibility impact, not by effort. Cartridge/banking *mappers* are the
dominant lever — a missing mapper means the image cannot be addressed at all, so it never runs;
audio/timing variants only change fidelity once a title already boots, so they rank last.

Each task names the existing chip it extends, the ROMs it unblocks, and an acceptance check.
Mapper/cart variants are auto-selected from the image (header field or CRT type id) the same way
`sms_mapper` vs `codemasters.mapper` is already auto-detected, with a manifest/CLI force-override.

Current baseline (do not re-implement):
- C64 `commodore.cartridge`: `generic` (8K/16K/ultimax), `ocean`, `magic_desk`, `easyflash`.
- SMS: `sega.sms_mapper` + `codemasters.mapper` (header auto-detect + force).
- Genesis: SSF2 >4 MiB banking, header SRAM/battery, I2C EEPROM (`24C01`–`24C65`, serial-keyed).

## P0 — Boot-blockers across whole catalogs (highest coverage per task)

- [x] **C64 cartridge CRT types**: add the high-frequency `.crt` hardware ids that currently fail
      to map, each a bank-select decode over `c64_cartridge`'s ROML/ROMH machinery. *Unblocks:* a
      large slice of the commercial C64 cart library. All eight runtime types now modelled
      (generic, Ocean, Fun Play, Super Games, System 3/C64GS, Dinamic, Zaxxon, Magic Desk,
      Comal-80, EasyFlash); remaining work is the data-gated golden boots.
  - [x] `system_3`/`c64gs` (15): write `$DE00+bank` selects the bank (value ignored). 1 unit test.
  - [x] `dinamic` (17): read `$DE00+bank` selects the bank (bus floats high). 1 unit test.
  - [x] `fun_play`/`power_play` (7): `$DE00` scrambled bank `((v>>3)&7)|((v&1)<<3)`; `(v&0xC6)==
        0x86` releases both lines. 1 unit test.
  - [x] `super_games` (8): `$DF00` (I/O-2) bank = `v&3`, bit 2 set releases the lines (16K). 1 test.
  - [x] `comal_80` (21): `$DE00` value `$80-$83` validates + selects a 16K bank `v&3`. 1 unit test.
  - [x] `zaxxon`/`super_zaxxon` (18): a ROML read latches the ROMH bank from address bit 12; the
        4 KiB ROML mirrors across $8000-$9FFF. `read_roml` made non-const for the read side effect.
        1 unit test.
  - [ ] Per-type golden boot once a ROM is supplied (data-gated, like the existing boot tests).
- [~] **SMS Korean mappers**: add the Korean families as distinct mapper chips (peer to
      `codemasters.mapper`). Header has no signature, so detect by ROM size + reset-vector
      heuristics with a force-override.
      *Unblocks:* effectively the entire Korean SMS catalog (currently unbootable).
  - [x] Standard Korean mapper (`korean.mapper`): slots 0/1 fixed to banks 0/1, slot 2 banked by a
        write to `$A000`, 16 KiB pages, linear power-on. Standalone chip + 7 unit tests.
  - [x] Wire into `sms_system`/`sms_runtime`: `sms_config::mapper::korean` (force-only -- no header
        signature, so `automatic` never picks it), Korean manifest TOMLs (NTSC/PAL) + embedded gen,
        both assembly paths, a `--mapper sega|codemasters|korean` player CLI override, and a Korean
        case in the assemble + runtime-parity tests.
  - [x] Korean MSX 8K mapper (`korean.msx_mapper`): $0000-$3FFF fixed bank 0, four 8 KiB windows
        banked by registers `$0000-$0003` (order 0/1/2/3 -> $8000/$A000/$4000/$6000), Nemesis
        variant maps the last 8 KiB at $0000-$1FFF. Standalone chip + 7 unit tests, wired end-to-end
        (`sms_config::mapper::korean_msx[_nemesis]`, manifest TOMLs, `--mapper korean-msx[-nemesis]`,
        assemble + runtime-parity coverage incl. the Nemesis variant).
  - [~] Korean `188-in-1`, `4PAK All Action`, `Janggun` multicart/special mappers.
    - [x] HiCom `188-in-1` (`korean.hicom_mapper`): 32 KiB page register at `$FFFF`, page mapped at
          `$0000-$7FFF` + 16 KiB mirror at `$8000-$BFFF`. Standalone chip + 6 unit tests, wired
          end-to-end (`sms_config::mapper::korean_hicom`, NTSC/PAL manifest TOMLs with the `$FFFF`
          register overlay -- the Sega-mapper scheme, `--mapper korean-hicom`, assemble +
          runtime-parity coverage).
    - [x] Multi `4x8K` (`korean.multi_4x8k_mapper`): the $2000-register XOR multicarts (128 Hap,
          Game Mo-eumjip 188 Hap) -- one $2000 write banks all four 8 KiB windows from the value
          XORed with {0x1F,0x1E,0x1D,0x1C}; $0000-$3FFF fixed (banks 0/1). Standalone chip + 6 unit
          tests. (System wiring -- the $0000-$BFFF cart MMIO catches the in-window $2000 register --
          is the follow-up.) NOTE: this is NOT "4-Pak All Action"; the original entry conflated them.
    - [ ] `4-Pak All Action` proper: a 16 KiB mapper (registers $3FFE/$7FFF/$BFFF) reusing the Sega
          16K machinery (control register, cart RAM, page-increment) -- distinct from the multi_4x8k
          above. Needs a deeper spec pass before implementing.
    - [x] True `Janggun` (`korean.janggun_mapper`): 8 KiB banks, $0000-$3FFF fixed (banks 0/1), four
          windows via direct selects ($4000/$6000/$8000/$A000) + Sega-style 16 KiB pairs ($FFFE/$FFFF
          in the work-RAM mirror); per-16 KiB-page bit-reversed reads (low-window FCR bit 7). Distinct
          from the Nemesis remap in `korean.msx_mapper`. Standalone chip + 8 unit tests, wired
          end-to-end (`sms_config::mapper::korean_janggun`, NTSC/PAL manifest TOMLs with the
          $FFFE-$FFFF register overlay, `--mapper korean-janggun`, assemble + runtime-parity coverage).
  - [ ] Auto-detection (CRC database) so Korean carts resolve without an explicit `--mapper`, plus
        a data-gated boot golden.
- [ ] **Genesis J-Cart**: extra two controller ports mapped at the top of ROM space (`$38FFFE`
      region). Extends `genesis_cart`/banking + input wiring.
      *Unblocks:* EA J-Cart titles (Micro Machines 2/'96/Military, Pete Sampras Tennis) — they
      boot today but only see 2 of 4 pads.
      *Accept:* system test reads pads 3/4 through the J-Cart port; existing 2-pad path unchanged.
- [ ] **Genesis Lock-on (Sonic & Knuckles)**: pass-through cartridge that maps a second ROM
      (`$300000` window) and exposes the combined image. Extends `genesis_cart` mapping.
      *Unblocks:* S&K lock-on with Sonic 2/3 (and the "blue sphere" no-cart mode).
      *Accept:* combined-image system test boots with both ROMs present (data-gated).

## P1 — High-profile titles & unlicensed/protection compatibility

- [ ] **Genesis SVP coprocessor** (`samsung.svp` DSP): the Virtua Racing cart. Large, isolated
      effort — a new chip in `chips/cpu` or `chips/coprocessor` plus the DMA/bank glue.
      *Unblocks:* Virtua Racing (the only retail SVP title).
      *Accept:* SVP instruction conformance subset + a data-gated boot golden.
- [ ] **Genesis pirate/protection bankswitch mappers**: Realtec, Kaiser/"Squirrel King", and the
      common multicart `$A130xx` protection-register families, as a `genesis_protection_mapper`
      variant set.
      *Unblocks:* unlicensed/bootleg + multicart images that currently read open bus and hang.
      *Accept:* per-family banking/protection-register unit tests.
- [ ] **SMS 93C46 EEPROM saves**: wire the serial 93C46 onto the SMS cart path (analogous to the
      Genesis I2C EEPROM) for the handful of titles that save to it.
      *Unblocks:* Sega Game/World-class EEPROM SMS saves (e.g. *Pro Yakyuu*).
      *Accept:* EEPROM read/write round-trip + battery-save persistence test.
- [ ] **Game Gear VDP variant** (`sega.315_5378`): extended CRAM (12-bit, 4096-color) + the GG
      reduced 160×144 viewport/border crop as a mode of `sms_vdp`.
      *Unblocks:* the Game Gear ROM catalog (a new system the SMS core nearly covers).
      *Accept:* CRAM-depth + viewport tests; a GG boot golden (data-gated).

## P2 — Fidelity & edge-case compatibility (title already boots)

- [ ] **SN76489 PSG variant select**: model the Genesis vs SMS noise-tap/clock difference (and
      the SMS-vs-GG stereo register) as an explicit `enum class variant` on `sn76489`, set from
      the manifest. *Impact:* audio correctness in the shared PSG, not boot.
- [ ] **Genesis VDP revision quirks** (`vdp1` vs `vdp2`): expose a revision selector for the
      handful of games that depend on the early-VDP fill/DMA timing or the masking-mode 0
      sprite quirk. *Impact:* a few edge-case titles' raster effects.
- [ ] **C64 freezer/utility carts** (Action Replay, Final Cartridge III, Retro Replay): NMI +
      RAM-overlay freezer hardware over `c64_cartridge`. *Impact:* utility carts and their
      freeze/restore; not a game-boot blocker.
- [ ] **SID 6581 vs 8580 filter-curve fidelity**: the variant *exists*; tune the filter cutoff
      curve + combined-waveform tables per part. *Impact:* audio accuracy only.
- [ ] **VIC-II revision timing fidelity**: the four revisions *exist*; close any remaining
      cycle-exact gaps the NTSC `6567r56a`/`6567r8` geometry needs. *Impact:* demo/raster-trick
      compatibility, not ROM boot.

## Build-first order (max compatibility per unit of work)

1. **P0 C64 CRT types** + **P0 SMS Korean mappers** — biggest catalog unlock; pure banking, no
   new clocked hardware, reuse existing cart/mapper machinery.
2. **P0 Genesis J-Cart + Lock-on** — high-profile titles, small surface.
3. **P1 EEPROM/protection mappers** — saves + unlicensed coverage.
4. **P1 Game Gear VDP** then **P1 SVP** — net-new systems/titles, larger effort.
5. **P2** — fidelity passes once the boot/save matrix is green.
