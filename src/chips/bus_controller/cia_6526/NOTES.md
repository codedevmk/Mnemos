# MOS 6526 (CIA) — Implementation Notes

The Complex Interface Adapter: two 8-bit ports with DDR, two 16-bit interval
timers, a BCD time-of-day clock with alarm, an 8-bit serial shift register, and
the interrupt-control register. The C64 uses two — CIA1 (/IRQ → CPU) and CIA2
(/IRQ → CPU NMI).

## Provenance

Ported from the Emu clean-room reference core (`Emu/Emu/chips/cia6526/`, Emu's
ADR-0008), relicensed into Mnemos per ADR 0006. Emu's source provenance
(MOS 6526 datasheet, C64 PRG §6, Wolfgang Lorenz suite as oracle, public
die-shot analyses; no GPL emulator source) carries over.

## Coverage

- Ports A/B with DDR; live input via host callbacks, output-change callbacks;
  PB6/PB7 timer output (pulse/toggle) override under CRx.PBON.
- Timers A/B: continuous/one-shot, φ2 / CNT / Timer-A-cascade input modes, the
  silicon-accurate **force-load pipeline** (LOAD → first underflow at C_w+N+3)
  and **start-delay** gating that the Lorenz timing suites verify.
- TOD: 50/60 Hz divider, BCD tenths/sec/min/hr with AM/PM, alarm match IRQ,
  latched read (HR freezes digits until TEN), write-freeze (HR..TEN staging).
- Serial shift register: input (sample SP on rising CNT) and output (clocked by
  Timer A toggle) with SDR-complete IRQ.
- Interrupt control: **NMOS edge-triggered IR flip-flop** with a 1-φ2 /IRQ
  propagation delay, read-to-clear ICR, set/clear mask writes. Enabling a mask
  after its source already latched does *not* raise /IRQ (the Lorenz `imr`
  behaviour); the HMOS 8521 level-driven variant is selectable but currently
  behaves as NMOS (deferred).

## Wiring (M3) and deferred

- `tick()` advances one φ2 cycle. Host wiring (port callbacks, IRQ routing to the
  CPU IRQ/NMI line, FLAG/CNT/SP pins) is supplied via `configure()`; the C64
  system shell wires it in M3.
- Save/load defers to the M3 runtime save-state format.
- Full Lorenz CIA-suite validation is program-based (needs the CPU + memory) and
  happens at C64 system integration (M3); the unit tests here cover timer modes,
  cascade, IRQ edge semantics, TOD, ports, and the shift register directly.
