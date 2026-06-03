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
- [ ] Bootstrap `tracer.exe`: `LoadLibrary(emulator.dll)`, inject allocator, drive run/step.
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
  - [ ] Write-watch wired to a real bank (e.g. work-RAM `$FFF7F5`/`$FFF809`) via the binding.
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
