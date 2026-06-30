# Idea Tree

**Baseline**: blocked:null | **Trunk**: blocked:null

## ROOT: Research session [PENDING]

### 1: Mechanism: Hardware-truth conformance harness that runs fixed-cycle headless tests and emits trace, framebuffer, audio, ACS, PTR, and regression-veto data.
Hypothesis: The current bottleneck is wrong objective and feedback: repo evidence shows rich Amiga unit/player tests, but no runnable ACS/PTR harness or Tier-A golden split, so changes cannot be safely optimized against hardware truth.
Observable: On a500_ocs_ce B_dev, `run_eval` becomes deterministic across repeats and reports per-test trace/frame/audio scores plus PTR with no protected-data edits.
Conflicts: none - attacks the measurement axis before code optimization. [PENDING]

**Insight**: Prerequisite plan persisted at docs/plans/2026-06-30-amiga-arbor-eval-precondition.md. Current blockers: no tracked tools/conformance/run_eval.py, no tracked Tier-A or WinUAE baseline testdata, and Amiga fixed-cycle eval cannot be satisfied by mnemos_player because --run-cycles is CPS2-only.

### 2: Mechanism: Beam-cycle bus-slot ledger shared by MC68000, Agnus display DMA, Copper, blitter, sprite, audio, and disk DMA.
Hypothesis: The current bottleneck is wrong credit assignment across timing owners: Amiga timing is spread across Agnus callbacks, `amiga_system` DMA state, and m68000 bus-wait callbacks, so a single slot ledger should make contention and wait attribution exact.
Observable: On a500_ocs_ce B_dev, cycle-stamped CPU wait, Copper wake, blitter busy, display DMA, and sprite/DMA-slot trace cases improve without PTR falling below the floor.
Conflicts: none - attacks an unexplored timing-representation axis. [PENDING]

### 3: Mechanism: Prefetch-exact MC68000 bus-cycle microstate integrated into Amiga board memory and exception policy.
Hypothesis: The current bottleneck is wrong representation of CPU/chip timing: the m68000 conformance harness intentionally skips PC, prefetch queue, cycle traces, and group-0 cases, which blocks cycle-exact Amiga CPU-DMA race proof.
Observable: On B_dev, m68000 conformance can enable prefetch/cycle comparison for Amiga-relevant cases and Amiga traces show correct CPU bus phases at custom-chip and chip-RAM boundaries.
Conflicts: none - attacks CPU microstate rather than chipset behavior. [PENDING]

### 4: Mechanism: Authoritative OCS pixel pipeline split where Agnus owns fetch timing and Denise owns serialization, priority, collision, HAM/EHB, and final framebuffer production.
Hypothesis: The current bottleneck is wrong display representation: Agnus currently folds planar fetch, color decode, sprites, collisions, and framebuffer output while Denise also models a serializer surface, so beam-racing and priority bugs can hide between duplicated responsibilities.
Observable: On a500_ocs_ce B_dev, frame-distance and trace scores improve for bitplane delay, dual-playfield priority, sprite priority, collision latches, HAM, and EHB cases.
Conflicts: none - attacks video ownership and representation rather than bus scheduling. [PENDING]

### 5: Mechanism: Paula event-calendar model for DMA fetch, period countdown, AUDxDAT reload, volume/period writes, interrupt assertion, and audio sample emission.
Hypothesis: The current bottleneck is wrong action granularity for audio timing: Paula exposes DMA and capture, but superiority requires sub-period reload and interrupt-cycle behavior that frame/audio hashes alone will not localize.
Observable: On a500_ocs_ce B_dev, audio trace conformance and structural audio distance improve on period reload, volume change, loop wrap, and AUDx interrupt micro-tests while ACS gains are not bought with PTR regression.
Conflicts: none - attacks the audio temporal model. [PENDING]

### 6: Mechanism: Disk bitcell/flux state machine with explicit MFM decode, wordsync, write-splice, weak-bit, index, side, and step timing events.
Hypothesis: The current bottleneck is wrong abstraction boundary for storage: the Amiga system has extensive ADF/raw-track logic, but conformance-grade disk behavior depends on bitcell phase and motor/index/CIA coupling rather than sector-level success.
Observable: On a500_ocs_ce B_dev, disk DMA trace and compatibility checkpoints improve for read pacing, WORDSYNC, write DMA, weak bits, side changes, and save-state resume without regressing previously passing disk cases.
Conflicts: none - attacks disk media representation and glue timing. [PENDING]
