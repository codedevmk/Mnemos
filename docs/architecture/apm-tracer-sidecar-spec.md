# APM Tracer Sidecar — Architecture Spec

Status: design of record (pre-implementation). Companion to the task list in `apm-tracer-tasks.md`.

A **self-contained tracer capability** that loads the emulator as a **plugin**, gives it a
**custom tagged memory allocator**, and observes/records execution from the **outside as a
sidecar** — modelled on Dynatrace/Datadog APM. The emulator owns **zero** logging/observer/DB
logic; it just runs. Tracing is a standalone product in its own subfolder, linked into both our
engine *and* (later) the reference core via a pure-C ABI, so their traces are directly diffable.

Goal it serves: Genesis boot-frame parity is stuck at ~78% byte-exact ("C"); the dominant
remaining class is a sub-frame raster-phase fork from one root cause that resisted
hypothesis-by-hypothesis debugging. This toolkit makes root-causing a *bisect query* instead of
an hours-long re-run-and-grep loop, and pays off on every future divergence.

---

## Repo conventions that shape the layout

- **Tiered modules** (`mnemos_declare_tier`): every C++ target declares a TIER and may only
  depend on equal-or-lower tiers. Engine core is tiers 0–5, instrumentation/debug are 6–8, apps
  highest.
- **Existing neighbours**: `src/debug/` already has a debugger engine (PC breakpoints, memory
  watchpoints, single-step) and `debug_dump` (framebuffer→PPM, memory_view→.bin, CSV trace);
  `src/instrumentation/` provides `ichip_introspection`. The APM capability **composes with /
  does not duplicate** these — but it is a fundamentally different thing (an out-of-process
  sidecar host with its own allocator), so it lives apart.
- **Module pattern**: a folder = `CMakeLists.txt` + flat sources/headers + `tests/` + `README.md`;
  library `mnemos_<path>`, alias `mnemos::<path>`.
- The engine builds as **static libs**; apps are executables that link them. Root CMake adds
  `src`, `tools`, `tests`.

## Where it lives — top-level `apm/`, deliberately outside `src/`

`src/` *is* the engine. "Not mingled with our engine" ⇒ a **sibling** top-level `apm/`, added via
`add_subdirectory(apm)` in root CMake after `src`. It sits **outside the engine's tier DAG** — it
runtime-loads the engine as a DLL and link-depends on essentially nothing engine-side (one pure-C
ABI header + one DI interface header). It could be lifted into its own repo unchanged. That is the
strongest expression of the Datadog/Dynatrace separation.

## The structure

```
apm/                                  # self-contained capability (sibling to src/, tools/)
  CMakeLists.txt                      # add_subdirectory(abi host memory observe collect bindings)
  README.md                           # what it is + how to run the sidecar
  abi/                                # THE CONTRACT — pure C, zero deps, included by both sides
    apm_plugin_abi.h                  #   C-ABI: create(allocator)->handle, run_frame, step,
    apm_allocator_abi.h               #   load_rom, read_register, region/state query
    apm_tags.h                        #   bank tag ids (chip, bank, guest_base, size, space)
  host/                               # tracer.exe — the bootstrap / "APM agent"
    CMakeLists.txt                    #   -> mnemos_tracer (executable)
    tracer_main.cpp                   #   parse config, load plugin, drive run/step, flush sinks
    plugin_host.{hpp,cpp}             #   LoadLibrary, resolve factory, lifecycle (win32)
    session.{hpp,cpp}                 #   a capture session: config -> enabled streams -> sinks
    config.{hpp,cpp}                  #   what to trace (frames N..M, watch list, streams)
    tests/
  memory/                             # the specialized tagged allocator (the heart)
    CMakeLists.txt                    #   -> mnemos_apm_memory
    tagged_allocator.{hpp,cpp}        #   page-aligned tagged arenas; implements imemory_allocator
    bank_registry.{hpp,cpp}           #   tag <-> {guest_base,size,host_ptr}; reverse addr lookup
    page_guard.{hpp,cpp}              #   VirtualProtect + Vectored Exception Handler engine
    tests/
  observe/                            # the sidecar observers / watchers
    CMakeLists.txt                    #   -> mnemos_apm_observe
    boundary_differ.{hpp,cpp}         #   snapshot + diff tagged regions at frame/scanline/trigger
    write_watch.{hpp,cpp}             #   page-guard write watchpoints (PC via ABI read_register)
    read_watch.{hpp,cpp}              #   page-guard read watchpoints (poll + next-branch capture)
    value_watch.{hpp,cpp}             #   predicate watchers by tag+addr
    perf_counters.{hpp,cpp}           #   per-chip cycles/stalls/instr/faults/trace-bytes + emit
    tests/
  collect/                            # record schema + capture buffers + sinks
    CMakeLists.txt                    #   -> mnemos_apm_collect
    record.hpp                        #   universal record types, master-cycle indexed
    ring_buffer.{hpp,cpp}             #   capture buffer feeding the sink thread
    parquet_sink.{hpp,cpp}            #   columnar writer (10 GB-friendly)
    sqlite_sink.{hpp,cpp}             #   small-run sink
    emit.{hpp,cpp}                    #   emit/event API
    tests/
  bindings/                           # APM owns its glue to each emulator core
    genesis/
      CMakeLists.txt                  #   -> mnemos_genesis (SHARED lib = emulator.dll)
      genesis_binding.cpp             #   wraps assemble_genesis behind apm_plugin_abi,
                                      #   forwards injected allocator, exposes step/read_register
  query/                              # offline analysis — the payoff (Python + DuckDB)
    ingest.py  align.py  bisect.py  diff.py  README.md
```

## The engine-side change (the ONLY thing that touches `src/`)

Minimal and pure dependency-injection — **no tracing/observer/DB concept ever enters the engine**:

```
src/foundation/                       # tier 0 — lowest, so any chip/system can depend on it
  imemory_allocator.hpp               #   abstract: allocate_tagged(tag,size,align)/free/query
  memory_tag.hpp                      #   POD tag {chip, bank, guest_base, size, space}
```

Then chips/system **source their observed banks from the injected allocator** (page-aligned),
instead of inline `std::array`:

- `genesis_system`: `work_ram`, `z80_ram` → allocator-provided.
- `genesis_vdp`: `VRAM/CRAM/VSRAM` → allocator-provided (later milestone).
- **CPU/register state stays exactly as-is** — the tracer reads the guest PC via the ABI
  `read_register` call at fault time, so we do *not* have to relocate CPU internals. This shrinks
  the invasive part to just the data banks we actually watch.

Default: a tiny malloc-backed `imemory_allocator` so standalone engine runs/tests/the sweep stay
byte-identical. The tagged allocator is only injected when running under the tracer.

## Dependency direction (proves the engine stays pristine)

```
            apm/abi/*.h   (pure C, zero deps)
             ▲                       ▲
    includes │                       │ includes
  ┌──────────┴───────────┐   ┌───────┴────────────────┐
  │ apm/host, observe,   │   │ apm/bindings/genesis   │──links──▶ mnemos::manifests::genesis
  │ memory, collect      │   │   (emulator.dll)       │──impl───▶ src/foundation/imemory_allocator.hpp
  └──────────┬───────────┘   └────────────────────────┘
             │ runtime LoadLibrary (NO link-time dep on engine)
             ▼
       mnemos_genesis.dll
```

- **Engine core** depends only on its *own* `imemory_allocator.hpp`. Zero APM knowledge. ✓
- **APM host** depends on no engine code — it loads the DLL at runtime. ✓
- **The binding** is the single bridge (APM-owned, in `apm/`), so `src/` never references `apm/`. ✓

## The one law of physics this design works around

A guest write is just `host_bank[addr] = value` executed by the emulator's own C++ — nothing
outside the process sees it per-write for free. Three observation modes, each with a hard limit:

1. **Boundary diff** of tagged regions (frame/scanline/trigger): zero-cost, zero-seam, but coarse
   (what changed, not the sequence/PC).
2. **OS page-protection** on tagged pages (VirtualProtect + VEH): precise + zero-seam, but a fault
   is ~µs — perfect for a *handful* of watched addresses, fatal if applied to all banks.
3. **One in-emulator seam** (single-step entry / accessor hook): the only way to get the full
   per-instruction *opcode-execution sequence* cheaply (instruction fetch can't be page-faulted).

| Stream | Seam-free? | Mechanism |
|---|---|---|
| Tagged whole-memory snapshots + per-frame/scanline value diffs | yes | allocator owns banks; tracer diffs by tag |
| Perf counters, emit/event API, sidecar, storage/query/diff | yes | all in tracer |
| Cycle/PC-precise watchpoints on a few addresses | yes | page-protect those tagged pages; read guest PC via ABI `read_register` in the fault handler |
| Full per-instruction opcode *sequence* | 1 seam | single-step entry the plugin exports |

Universal index on every record: `(frame, master_cycle, scanline, hcounter, global_inst#)`. Align
on **master_cycle** (immune to the reference's instruction coalescing) — never raw instruction
index, never raw frame number (absorbs the +1/+2 harness offset).

## Naming / build conventions (matching the repo)

Targets `mnemos_apm_<part>` with aliases `mnemos::apm::<part>`; executable `mnemos_tracer`;
binding DLL `mnemos_genesis`. All use `mnemos_apply_common_target_options` for the shared
`/W4 /WX` flags. APM targets **opt out of `mnemos_declare_tier`** (they are outside the engine
DAG); only `src/foundation/imemory_allocator` participates, at tier 0.

## First slice to build (de-risk before touching the live engine)

1. `apm/abi/` + `apm/memory/page_guard` + a throwaway `apm/memory/tests/page_guard_proof.cpp` —
   demonstrate **VirtualProtect + VEH catches a write to a guarded page and recovers**, on a plain
   host array. No engine involved. (Validates the one piece that could be a dead end on Windows.)
2. `apm/memory/tagged_allocator` + `bank_registry` — page-aligned tagged arenas implementing the
   engine interface.
3. `src/foundation/imemory_allocator.hpp` + repoint `genesis_system::work_ram` to it (Phase 0,
   one bank, re-run parity guard).
4. `apm/bindings/genesis` DLL + `apm/host` tracer that loads it and arms a write-watch on
   `$FFF809` → prints writer PC. That is the current bug's tool, end-to-end.

## Build order (max leverage), cross-referenced to `apm-tracer-tasks.md`

1. **Phase 0** — pluginnable + allocator-fed engine (unblocks everything; clean DI refactor).
2. **Phase 1 + Phase 2 page-protection read/write watchpoints** — cracks the current bug seam-free.
3. **Phase 5 store + lockstep diff** — turns divergence-finding into a query.
4. **Phase 3 opcode-sequence** (the one seam) — full per-instruction parity.
5. **Phase 4 reference plugin** — symmetric, directly diffable.
6. **Phase 6** — control-flow / raster / timing depth.

## Current lead this unblocks (Kid Chameleon `$FFF809`)

- Aligned compare = `Mnemos f120 ≡ reference f121` (+1 offset). Divergent work-RAM: wave table
  `$FF4E5C-71` (same values, **shifted phase**) + phase counters `$FFF7F5`, `$FFF809`.
- Both engines write `$FFF809` **once per frame from the same `pc=$000488`**. Mnemos's counter is a
  **constant +8 ahead** (`$00` at M=frame24 vs ref frame32, holding +8 across the range). The
  counter is faithful — the fork is **when the boot phase that starts it completes**: Mnemos reaches
  it ~7–8 frames earlier, i.e. progresses through early boot faster (seeds near the frame-6
  VRAM-clear region).
- **Next disciplined step (Phase 2 read-watch):** page-protect VDP status, fault on the boot
  wait-loop's poll in both engines, find the read whose returned bit differs + the branch it flips —
  the root cause of the 8-frame lead, hence the wave-table phase shift.
