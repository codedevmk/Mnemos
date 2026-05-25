# Commodore 64 PLA (MOS 906114-01 / 82S100) — Implementation Notes

Pure combinational memory-banking decoder. Five logic inputs — the CPU-port bits
LORAM/HIRAM/CHAREN (low 3 bits of the 6510 $0001 port) plus the cartridge /GAME
and /EXROM lines — select one chip-select region per area of the address map.

## Provenance

Ported from the Emu reference core (`Emu/Emu/chips/pla/`), relicensed into Mnemos
per ADR 0006.

## Behavioral references

- MOS 906114-01 / 82S100 truth table.
- VICE `c64mem.c` / `c64memsc.c` (the open-bus / ultimax conventions).
- *Mapping the C64*, Appendix H.

## Coverage

- `decode_cpu_address`: RAM / BASIC / KERNAL / CHARGEN / I/O across the standard
  configuration, plus 8 KB (ROML) and 16 KB (ROML+ROMH) cartridge modes and the
  ultimax (`/GAME=0, /EXROM=1`) map (ROML, ROMH, I/O, and open-bus windows).
- `decode_vic_address`: the VIC-II private fetch path (RAM / CHARGEN, ignoring
  CHAREN and the CPU port; ultimax ROMH at $3000-$3FFF).
- `set_cpu_port` / `set_cart_lines` inputs; register-snapshot introspection.

## Wiring (M3)

`tick()` is a no-op — the PLA has no clocked state. Bus-side overlay control —
where the topology bus consumes `decode_cpu_address` / `decode_vic_address` to
switch region mappings — is an M3 topology concern. Save/load defers to the M3
runtime save-state format.
