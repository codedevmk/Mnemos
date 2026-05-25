# Commodore 64 ROMs

The C64 needs three MOS ROM images. **These are copyrighted and are never
committed to this repository.** Mnemos boots only when you supply your own
verified dump locally; CI keeps the golden-boot test data-gated and skips it when
the ROMs are absent.

## Required images

| Region  | Part        | Size  | Manifest path                   |
| ------- | ----------- | ----- | ------------------------------- |
| BASIC   | 901226-01   | 8 KiB | `roms/basic.901226-01.bin`      |
| KERNAL  | 901227-03   | 8 KiB | `roms/kernal.901227-03.bin`     |
| CHARGEN | 901225-01   | 4 KiB | `roms/character.901225-01.bin`  |

Paths are relative to the manifest (`c64.pal.toml`). KERNAL revision 03 is the
common late PAL/NTSC revision; earlier dumps (01/02) will hash differently and
must have their `sha256` updated to match.

## Acquisition

Legitimate sources include a dump from hardware you own, or the ROM set bundled
with VICE (the `C64` data directory). The standard images are also distributed
with several preservation projects. Acquire them however you are licensed to; do
not add them to git (`*.bin` under `roms/` is ignored).

## Verifying and recording hashes

`c64.pal.toml` ships with **placeholder** all-zero `sha256` fields. The loader
rejects any ROM whose SHA-256 does not match the manifest, so a placeholder
refuses every real image until you replace it. After placing your dumps:

```powershell
# Per file — paste the lowercase hex into the matching sha256 field.
(Get-FileHash -Algorithm SHA256 .\roms\basic.901226-01.bin).Hash.ToLower()
(Get-FileHash -Algorithm SHA256 .\roms\kernal.901227-03.bin).Hash.ToLower()
(Get-FileHash -Algorithm SHA256 .\roms\character.901225-01.bin).Hash.ToLower()
```

```bash
sha256sum roms/basic.901226-01.bin roms/kernal.901227-03.bin roms/character.901225-01.bin
```

The hashes are intrinsic to the ROM contents, so once you record them they pin
the exact images Mnemos will accept on every machine.

## Testing without ROMs

`assemble_c64()` takes the three images as arguments and does no hashing itself,
so unit tests pass zero-/pattern-filled buffers to exercise PLA banking without
any copyrighted data (see `tests/c64_system_test.cpp`). Hash verification lives
in the manifest loader path, which the data-gated golden-boot test drives.
