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

## Pre-swapped Work RAM (the reference emulator)

### What the reference does

In `core/macros.h`, with the `LSB_FIRST` define on a little-endian host:

```c
#define READ_BYTE(BASE, ADDR)        (BASE)[(ADDR)^1]
#define READ_WORD(BASE, ADDR)        (((BASE)[ADDR]<<8) | (BASE)[(ADDR)+1])
#define WRITE_BYTE(BASE, ADDR, VAL)  (BASE)[(ADDR)^1] = (VAL)&0xff
#define WRITE_WORD(BASE, ADDR, VAL)  (BASE)[ADDR] = ((VAL)>>8) & 0xff; \
                                     (BASE)[(ADDR)+1] = (VAL)&0xff
```

`work_ram[]` is stored such that a `*(uint16_t *)` read on a little-endian host
returns the **correct big-endian 68000 word value** with no shift / shuffle.
The cost is that byte-level access (the much-less-common path) has to flip the
low bit to compensate (`ADDR ^ 1`).

### Why Mnemos doesn't

The byte at `work_ram[ADDR]` is no longer the byte the 68000 wrote at that
address -- it's that byte's *neighbour* (`work_ram[ADDR^1]`). That means:

1. **A raw dump of `work_ram` is byte-swapped within every 16-bit word
   relative to what the 68000 actually wrote.** Compare a the reference `work_ram`
   blob to Mnemos's and 32 KiB of bytes look wrong, even when emulation is
   bit-perfect. (We lost an afternoon to this exact trap A/B'ing Blades of
   Vengeance -- two bytes "diverged" at `$FFE8FE/$FFE8FF`; after swapping
   pairs in the reference's dump they were identical.) For A/B against the reference, swap
   every (i, i+1) byte pair in the libretro `RETRO_MEMORY_SYSTEM_RAM`
   buffer before comparing.
2. Save states cannot be diffed across emulators byte-for-byte.
3. A memory inspector / debugger has to apply `^1` everywhere it reads, or
   show data the guest never wrote at those addresses.
4. Any code that does `memcpy(work_ram + addr, ...)` to inject data needs
   the trick applied at the call site -- easy to miss for the cart
   header, save-RAM, BIOS bootstrap blob, etc.

The whole motivation is to skip one shift + one or-byte per word read on the
old MIPS / SH-4 / 1.0 GHz x86 cores the reference originally targeted. On a 2026 host
the byte assembly we do in `m68000::rd16()` is one or two cycles per access
and well under the memory subsystem's noise floor for the per-frame budget.

**Rule for Mnemos:** the canonical in-memory layout is the guest's. Any
host-fast-path that needs a different layout is a *view*, not a *storage*
change, and must be reconstructed on demand -- not pre-baked.

---

## A/B harness implications

For the reference emulator specifically (`scripts/reference_runner.c` / `reference_runner32.exe`
+ the libretro DLL):

- `RETRO_MEMORY_SYSTEM_RAM` returns the `work_ram` buffer in the reference's internal
  byte-swapped layout, **not** the 68000-visible byte order.
- The current `reference_runner.c` dumps that buffer raw. Consumers comparing
  against Mnemos's `mnemos_player --screenshot` `.wram.bin` must byte-swap
  pairs in the the reference dump first.
- `RETRO_MEMORY_VIDEO_RAM` is not exposed by the the reference libretro core -- this
  is noted in `reference_runner.c` already.

A one-shot fix is to byte-swap in `dump_ppm()`'s sibling write of the WRAM
file, so the on-disk artefact matches the 68000's view. We have not done
this yet because the trap is documented here and the runner is dev-only,
but if A/B sessions start to add up the bytes per investigation, that's the
right place.

---

## Other tricks documented elsewhere

To be added as encountered:

- VDP CRAM / VSRAM pre-decoded to host pixel format (the reference, BlastEm)
- Z80 -> 68K bus access cycle accounting shortcuts
- Pre-decoded sprite tables
- DMA fill emulated as a single bulk memcpy instead of a per-byte stream

Each new entry should follow the same template: *what the other emulator
does*, *why Mnemos rejects it*, *what trap it lays for A/B work*.
