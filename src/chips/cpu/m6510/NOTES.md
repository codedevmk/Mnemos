# MOS 6510 — Implementation Notes

This chip is the 6502 core used in the Commodore 64, with an on-chip 8-bit I/O
port at $00/$01.

## Behavioral references

All behavior is reproduced from public documentation and validated against public
test ROMs; no emulator source is copied (see `AGENTS.md` §Dependency Policy).

- **Instruction set, cycle counts, addressing modes:** the MCS6500 Family
  Programming/Hardware Manuals and the widely-published 6502 opcode matrix.
- **Decimal mode (BCD ADC/SBC):** Bruce Clark, *Decimal Mode in the NMOS 6502*
  (the algorithm and the NMOS N/V/Z quirks).
- **Undocumented opcodes:** Wolfgang Lorenz / "No More Secrets" *NMOS 6510
  Unintended Opcodes*.
- **Conformance (planned, Task 16):** Klaus Dormann `6502_functional_test`
  ("2M65"); decimal and illegal-opcode suites obtained at CI time. ROMs are never
  committed.

## Architecture

- **Cycle-stepped:** `tick(n)` calls `step_one_cycle()` n times; each cycle
  performs at most one bus access so the scheduler/VIC-II can interleave cycles.
- **Table-driven decode:** a 256-entry table maps each opcode to
  `{operation, addressing_mode, access_kind, illegal}`; per-`access_kind` micro
  engines walk the standard cycle pattern and invoke small operation handlers.
- **Memory access:** `read`/`write` intercept the $00/$01 DDR/port latches
  (input pins read the default pull) before delegating to the attached `i_bus`.

## Coverage and known simplifications (v0.1)

- All 151 documented opcodes are implemented with cycle counts, including the
  page-cross read penalty, the always-paid store/RMW fixup cycle, the RMW
  dummy-write, and the JMP-(indirect) page-boundary bug.
- **Stable undocumented opcodes implemented:** LAX, SAX, DCP, ISC, SLO, RLA,
  SRE, RRA, ANC, ALR, ARR, SBX, the SBC ($EB) alias, and the undocumented NOPs
  (implied / #imm / zp / zp,X / abs / abs,X).
- **Unstable undocumented opcodes** (SHA/SHX/SHY, TAS, LAS, ANE/XAA, LXA) are
  implemented with the conventional deterministic model: ANE/LXA use a fixed
  "magic" constant ($EE); SHA/SHX/SHY/TAS store the source ANDed with
  (target-high + 1); TAS also sets SP = A & X; LAS ANDs memory with SP. The
  page-cross address corruption these exhibit on real silicon is not modelled.
  JAM/KIL still decode as illegal and end the instruction.
- **Port floating-gate fade:** the unconnected $01 bits 6 and 7, once driven as
  outputs then switched to inputs, hold their last value for ~port_falloff_cycles
  then read 0 (the NMOS capacitive decay); bits 0-5 read the pull-up as before.
- **Interrupts** are polled at the instruction boundary (a simplification of the
  real mid-instruction polling). IRQ/NMI run the cycle-accurate 7-cycle sequence;
  RES is functional via `reset()`. Exact interrupt-timing edge cases will be
  validated by the conformance ROMs.
- **Save/load state** is deferred to M3 (runtime save-state format); the chunk
  layout is recorded in `docs/plans/2026-05-22-m6510-opcode-engine.md`.
