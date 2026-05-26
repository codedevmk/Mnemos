# MOS 6581 / 8580 (SID) — Implementation Notes

The Commodore 64 Sound Interface Device: three voices, a multi-mode filter, and a
mixed audio output. PAL φ2 (985248 Hz) is the default sample rate.

## Provenance

Clean-room implementation. Source ledger: MOS 6581 datasheet for the register
map and ADSR rate periods; Bob Yannes public interviews for envelope staging
and waveform AND-combination; no reSID/GPL source. This satisfies the M2
"published SID emulation note set" requirement.

## Coverage

- Register surface: 3 voices × 7 + filter/volume + read-only status, with the
  32-byte alias across `$D400`–`$D7FF`, write-only reads floating high, and
  read-only-register write protection.
- ADSR envelope generator with the datasheet rate-period table and the
  exponential decay/release divider; gate re-trigger and sustain tracking.
- Waveform generators: triangle, sawtooth, pulse, noise (23-bit LFSR), combined
  waveforms (AND intersection; 8580 partial-restore approximation), ring
  modulation, and hard sync.
- Multi-mode state-variable filter (LP/BP/HP) with per-variant cutoff range and
  resonance; master volume; MUTE3.
- OSC3 (`$D41B`) / ENV3 (`$D41C`) readback; `sample()` mixed signed-16-bit out.

## Variants and tolerances

6581 vs 8580 differ in filter range (6581 ~220 Hz–18 kHz, 8580 ~30 Hz–12 kHz),
combined-waveform fullness, and noise corruption. The 8580 combined-waveform and
filter curves are documented approximations within a ±10% tolerance,
not measured wavetables.

## Wiring (M3) and deferred

- `tick()` advances one φ2 cycle (envelopes + oscillators). The host reads
  `sample()` at its mixer rate (`set_sample_rate`); paddles via `set_paddle_*`.
- Save/load defers to the M3 runtime save-state format.
- The M2 acceptance "SID register interface validated against a reference register
  trace from a known SID tune" is a system-level test (needs a player + trace);
  it lands at C64 integration (M3). Unit tests here cover register classes,
  waveform/ADSR/sync/OSC3-ENV3/filter/sample directly.
