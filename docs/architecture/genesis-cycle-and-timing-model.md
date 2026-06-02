# Genesis / Mega Drive — Cycle & Timing Model

> Reference for how Mnemos processes cycles end-to-end on the Sega Genesis: the master
> clock and scheduler dividers, the 68000's instruction-atomic execution model, the VDP
> frame/line timing, and the VDP→CPU bus-stall mechanisms.
>
> **Purpose:** understand cycle processing without re-reading the whole engine. Sections
> are keyed on **function names** (stable across edits) with approximate line hints (which
> drift). Treat this as a living document — append findings as the timing model is refined.
>
> Related: ADR-0004 (chip contract), ADR-0005 (scheduler strategy).

---

## 1. Master clock and scheduler dividers

Everything is paced in **master clocks**. Each chip carries an integer **divider** and ticks
once every `divider` master cycles. The Genesis schedule (`genesis_runtime::schedule()`):

| Chip | Divider | Effective rate |
|------|---------|----------------|
| VDP  | 1  | master (frame source) |
| 68000 | 7 | master / 7 |
| Z80  | 15 | master / 15 |
| YM2612 (FM) | 7 | master / 7 |
| SN76489 (PSG) | 15 | master / 15 |

`runtime/scheduler.cpp`:

- **`run_frame()`** advances `run_master_cycles(1)` in a loop until the frame source (the VDP)
  increments its frame index — i.e. one video frame. The single-master-cycle granularity is what
  preserves CPU/VDP interleave within a frame.
- **`run_master_cycles(N)`** has two paths:
  - *Fast path* — if **all** dividers are 1, each chip gets one batched `tick(N)`.
  - *Per-cycle loop* — otherwise, for each of N master cycles, every chip's accumulator is
    incremented and the chip is ticked (`tick(1)`) when the accumulator reaches its divider.
  - The Genesis has mixed dividers, so it uses the **per-cycle loop**: every master cycle ticks the
    VDP; every 7th additionally ticks the 68000; every 15th the Z80; and so on.

## 2. 68000 execution — instruction-atomic (`chips/cpu/m68000/m68000.cpp`)

```
tick(N):            cycle_debt_ += N;  while (cycle_debt_ > 0) cycle_debt_ -= step_instruction();
step_instruction(): cycles_ = 0;  ... accumulate this instruction's cost in cycles_ ...
                    elapsed_ += cycles_;  return cycles_;
```

- Cost accounting is **additive**: bus accesses add to `cycles_` (`read8`/`write8` `cycles_ += 4`,
  `read32 += 8`), plus effective-address, idle, and refresh adders.
- **Meaning:** the CPU executes a *whole instruction* in one `tick(1)` call (atomically), then
  "owes" its cost — `cycle_debt_` goes negative by `cost - 1`, so the CPU does not execute again
  for `cost` ticks (= `cost` CPU cycles = `cost × 7` master). Instructions are therefore spaced by
  their cost, but each instruction's bus effects (including VDP writes) land at the *single* master
  cycle of its tick, not spread across the cost window.

### Counters

| Field | Meaning |
|-------|---------|
| `cycles_` | Cost accumulator for the **current** instruction (reset to 0 each `step_instruction`). |
| `elapsed_` | Cumulative CPU cycles over the chip's lifetime. |
| `cycle_debt_` | Execution-pacing budget consumed by `tick`. |
| `inst_addr_` | PC of the **in-flight** instruction (`current_instruction_addr()`) — the true writer PC for watchpoints, distinct from the already-advanced `cpu_registers().pc`. |

### Gotchas

- A few instructions **set `cycles_ = N` absolutely** (e.g. `Bcc`/`RTS`/`JMP`) rather than `+=`.
  Any externally injected wait-cycles must therefore be added **during a bus access** so a later
  absolute-set instruction cannot clobber them.
- **Wait-state precedent:** the Z80-bus access latency adds `cycles_ += 1` in `read8`/`write8` when
  the address is in the Z80 bus window — the established pattern for charging a per-access stall to
  the executing instruction.
- **Refresh:** `bus_refresh_due_` uses a *sliding* schedule (`= elapsed_ + 128`) and adds a small
  fixed cost to an instruction when due. The refresh **count** (not just its position) is
  cycle-significant — an absolute (vs sliding) schedule over-counts refreshes and accrues a
  cumulative boot drift.

## 3. CPU ↔ chip wiring — two construction paths

The Genesis can be assembled two ways, and they are required to be byte-identical
(`genesis_manifest_parity_test`):

- **`assemble_genesis`** (`genesis_system.cpp`) — wires the VDP/CPU directly via setters:
  `vdp.set_irq_callback`, `set_dma_read`, `set_delayed_irq_callback`, `set_vblank_callback`,
  `cpu.set_irq_ack_callback`. The CPU is gated by
  `predicate_gated_chip cpu_gate{cpu, cpu_runnable=!vdp.dma_stall_active()}`.
- **`build_genesis_runtime`** (`genesis_callbacks.cpp` `make_genesis_host_tables`) — registers
  **named** callbacks (`"genesis.vdp_irq"` → `cpu.set_irq_level`, `"genesis.vdp_delayed_irq"`,
  `"genesis.vblank"`, `"genesis.irq_ack"`, `"genesis.dma_read"`) and predicates
  (`"genesis.cpu_runnable"` → `!vdp->dma_stall_active()`, `"genesis.z80_running"`) plus the MMIO
  factories; `build_system` connects the named hooks to chip setters through the manifest. **This is
  the path the player (and therefore the parity harness) uses.**

> Adding a new VDP→CPU signal means wiring **both** paths, or doing it in the shared MMIO handler.

## 4. VDP bus-stall mechanisms (`chips/video/genesis_vdp`)

Three **separate** master-cycle timers, all drained in `genesis_vdp::tick()`:

| Timer | Source | Gates the 68000? |
|-------|--------|------------------|
| `dma_stall_master_cycles_` | type-0/1 DMA (68K→VRAM/CRAM/VSRAM), armed at the DMA trigger via `estimate_dma_transfer_cycles(len, type)` | **Yes** |
| `fifo_stall_master_cycles_` | write-FIFO back-pressure, armed in `fifo_data_write()` when the 4-entry FIFO is full **during active display** (skipped when `in_vblank_` or display off) | **Yes** |
| `dma_busy_master_cycles_` | type-2/3 DMA (VRAM fill/copy) busy-**status** hold | **No** — the CPU runs free; games just poll the busy bit |

- The gate predicate is `dma_stall_active()` = `dma_stall_master_cycles_ > 0 || fifo_stall_master_cycles_ > 0`.
- **Gating** means the scheduler **skip-ticks** the 68000 while a stall timer is positive (the
  `cpu_runnable` predicate returns false). Two consequences worth knowing:
  1. The stall is in **master** cycles applied by skipping CPU ticks, so the CPU's `cycles_`/
     `elapsed_` do **not** advance during the stall (the CPU isn't ticked). A gated stall therefore
     leaves **no cycle gap** in a CPU instruction trace.
  2. Because the CPU only ticks every 7 master, resume from a gated stall rounds to the next
     master/7 boundary. (Charging the stall to the writing instruction instead — making it
     "cycle-exact" — was tried and measured to *not* change parity on the known regression cases, so
     the resume-rounding is not currently believed to be a parity driver.)
- The FIFO model positions 4-entry drain times against the per-line access-slot tables
  (`fifo_timing_h40` / `fifo_timing_h32`) relative to `current_line_master()`.

## 5. VDP frame / line timing

- A scanline is a **fixed 3420 master clocks** for both H32 and H40 — only the dot clock and
  visible width differ. `total_hclocks()` = 342 (H32) / 420 (H40); `visible_width()` = 256 / 320.
- **NTSC frame** = 262 lines × 3420 = **896,040 master ≈ 128,006 CPU cycles**. **PAL** = 313 lines.
- `h40_mode()` = `(reg_[12] & 0x81) != 0`. Register `$0C` bit 0 = RS0, bit 7 = RS1; H40 requires
  **both** (`$81`). *Latent edge case:* the `!= 0` test also treats `$01`/`$80` as H40 (hardware
  selects H40 only on `$81`), but no known title writes those values, so it is currently cosmetic.
- The V-blank interrupt asserts at `vint_pending_delay_master_` = **770 (H32) / 788 (H40)** master
  of the V-blank-entry line.
- `genesis_vdp::reset()` does `reg_.fill(0)` — all VDP registers power on at 0 (so H32 is the
  power-on width).

## 6. Cycle units (for trace/parity work)

- Mnemos `cycles_` / `elapsed_` are **CPU cycles** (= master / 7).
- A per-instruction CPU trace is emitted by the headless render path: `player --screenshot <out> …`
  writes `<out>.cpu_trace.csv` with `frame,inst,pc,cycles` (via `debug/trace_csv_session`), plus
  VDP state sidecars `<out>.315_5313.{vram,cram,vsram,vdpregs}.bin` (the Genesis VDP's chip id is
  `315_5313`).
- When validating against an **external reference** emulator's cycle trace, account for the common
  differences: a reference may count in **master** cycles (multiply Mnemos by 7), may **reset its
  cycle counter every frame** (so a large negative inter-instruction "cost" at a frame boundary is
  just the reset), and may present a **leading start-up frame** that advances no game logic (so its
  frame *N+1* corresponds to Mnemos frame *N*). Compare with that frame offset, never same-frame.
- Diagnostic note: a **gated** stall leaves no cycle gap in the CPU trace (the CPU isn't ticked),
  whereas an in-instruction wait-state would show as a larger inter-instruction cycle delta —
  useful for telling the two apart. A frame-duration metric (cumulative cycle delta per frame) is
  robust even when an external trace coalesces instruction logging during VDP-write stalls.

## 7. Open items (enrich as discovered)

- A residual sub-frame **raster-phase** divergence on certain titles (a per-line CRAM/horizontal-
  scroll write landing at a slightly different scanline) is **not** explained by CPU or VDP stall
  timing — it survives ruling out per-opcode cost, FIFO display-off gating, FIFO stall magnitude,
  and gating-vs-cycle-exact resume. Next angle: instrument CRAM/VSRAM writes together with their
  `(line, hcounter)` and compare the exact write scanline.
- Z80 / YM2612 / SN76489 cycle models are not yet documented here — add when investigated.
- The DMA cost-rate formula (`estimate_dma_transfer_cycles`) should be folded in here when next
  touched.
