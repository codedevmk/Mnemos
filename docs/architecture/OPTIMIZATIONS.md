# Optimizations -- techniques other emulators use, and what we trade for clarity

Mnemos has a single overriding principle: **the in-memory layout of any chip's
visible state matches the chip's view, not the host CPU's**. We do not
byte-swap, pre-shift, pre-mask, or otherwise mangle bytes to make a host-side
fast path faster. The cost is a couple of nanoseconds per access; the win is
that a save state, a memory dump, or a debugger view is exactly the same bytes
the guest program would see.

This file collects the tricks other emulators play for speed, and the reason
Mnemos rejects each one. It also flags the engineering traps those tricks lay
for anyone trying to A/B against them.

---

## Pre-swapped Work RAM

### What the trick is

Some emulators store the 68000 work RAM byte-swapped so that a `*(uint16_t *)`
read on a little-endian host returns the correct big-endian word with no shift
or shuffle -- a read becomes `base[addr ^ 1]` at byte granularity and a plain
aligned load at word granularity. `work_ram[]` then yields the **correct
big-endian 68000 word value** for free; the cost is that byte-level access (the
much-less-common path) has to flip the low bit to compensate (`addr ^ 1`).

### Why Mnemos doesn't

The byte at `work_ram[addr]` is no longer the byte the 68000 wrote at that
address -- it's that byte's *neighbour* (`work_ram[addr ^ 1]`). That means:

1. **A raw dump of `work_ram` is byte-swapped within every 16-bit word
   relative to what the 68000 actually wrote** -- 32 KiB of bytes look wrong
   even when emulation is bit-perfect, and any cross-tool byte comparison has
   to swap pairs first.
2. Save states cannot be diffed byte-for-byte.
3. A memory inspector / debugger has to apply `^1` everywhere it reads, or
   show data the guest never wrote at those addresses.
4. Any code that does `memcpy(work_ram + addr, ...)` to inject data needs
   the trick applied at the call site -- easy to miss for the cart
   header, save-RAM, BIOS bootstrap blob, etc.

The whole motivation is to skip one shift + one or-byte per word read, which
mattered on the older, slower cores this trick originated on. On a 2026 host
the byte assembly we do in `m68000::rd16()` is one or two cycles per access
and well under the memory subsystem's noise floor for the per-frame budget.

**Rule for Mnemos:** the canonical in-memory layout is the guest's. Any
host-fast-path that needs a different layout is a *view*, not a *storage*
change, and must be reconstructed on demand -- not pre-baked.

---

## Other tricks documented elsewhere

To be added as encountered:

- VDP CRAM / VSRAM pre-decoded to host pixel format
- Z80 -> 68K bus access cycle accounting shortcuts
- Pre-decoded sprite tables
- DMA fill emulated as a single bulk memcpy instead of a per-byte stream

Each new entry should follow the same template: *what the other emulator
does*, *why Mnemos rejects it*, *what trap it lays for A/B work*.
