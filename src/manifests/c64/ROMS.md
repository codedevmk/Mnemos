# Commodore 64 ROMs

The C64 needs three MOS ROM images (BASIC, KERNAL, character generator).
**These are copyrighted and are never committed to this repository.** Mnemos
boots only when you supply your own verified dumps locally; CI keeps the
golden-boot test data-gated and skips it when the ROMs are absent.

## Required images

| Region  | Part        | Size  | Default manifest filename |
| ------- | ----------- | ----- | ------------------------- |
| BASIC   | 901226-01   | 8 KiB | `basic.bin`               |
| KERNAL  | 901227-03   | 8 KiB | `kernal.bin`              |
| CHARGEN | 901225-01   | 4 KiB | `chargen.bin`             |

Part numbers identify the hardware ROM revision; KERNAL revision 03 is the
common late PAL/NTSC revision (earlier dumps hash differently and need their
`sha256` updated to match).

The `file` field in `c64.pal.toml` / `c64.ntsc.toml` is just a default — you
may point it at any filename. The loader keys verification off `sha256`, not
the filename.

## Acquisition

Acquire the dumps however you are licensed to (a dump from hardware you own is
the canonical path). Do not add them to git — `*.bin` under `roms/` and the
top-level `roms/` directory are both gitignored.

## Verifying and recording hashes

The shipped manifests have **placeholder** all-zero `sha256` fields. The loader
rejects any ROM whose SHA-256 does not match the manifest, so a placeholder
refuses every real image until you replace it. After placing your dumps under
whatever path your local manifest points at:

```powershell
# Paste the lowercase hex into the matching sha256 field.
Get-FileHash -Algorithm SHA256 .\<path-to>\basic.bin   | ForEach-Object { $_.Hash.ToLower() }
Get-FileHash -Algorithm SHA256 .\<path-to>\kernal.bin  | ForEach-Object { $_.Hash.ToLower() }
Get-FileHash -Algorithm SHA256 .\<path-to>\chargen.bin | ForEach-Object { $_.Hash.ToLower() }
```

```bash
sha256sum <path-to>/basic.bin <path-to>/kernal.bin <path-to>/chargen.bin
```

The hashes are intrinsic to the ROM contents, so once you record them they pin
the exact images Mnemos will accept on every machine.

## Testing without ROMs

`assemble_c64()` takes the three images as arguments and does no hashing
itself, so unit tests pass zero-/pattern-filled buffers to exercise PLA
banking without any copyrighted data (see `tests/c64_system_test.cpp`). Hash
verification lives in the manifest loader path, which the data-gated
golden-boot test drives.
