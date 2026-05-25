# Golden Tests

Frame-hash and replay golden tests live here. Proprietary ROMs and firmware are
never committed, so these tests are **data-gated**: they self-skip (CTest reports
*Skipped*, not failed) unless you point them at local data.

## `mnemos_c64_basic_boot_test`

Boots a real Commodore 64 from the reset vector, renders a fixed number of frames,
and hashes the framebuffer. The hash is deterministic for a given ROM set, so once
recorded it pins the whole boot path (PLA banking, KERNAL init, VIC raster, the
IRQ-driven cursor).

### ROMs

Put the three C64 ROM images in one directory and point `MNEMOS_C64_ROM_DIR` at
it. Accepted filenames (first match wins), sizes are checked:

| ROM     | size  | filenames tried                                                   |
|---------|-------|-------------------------------------------------------------------|
| BASIC   | 8 KiB | `basic.901226-01.bin`, `basic.bin`, `basic`                       |
| KERNAL  | 8 KiB | `kernal.901227-03.bin`, `kernal.bin`, `kernal`                    |
| CHARGEN | 4 KiB | `character.901225-01.bin`, `chargen.901225-01.bin`, `chargen.bin`, `char.bin` |

See `src/manifests/c64/ROMS.md` for acquisition + the canonical hashes.

### Environment variables

| Variable                 | Effect                                                       |
|--------------------------|-------------------------------------------------------------|
| `MNEMOS_C64_ROM_DIR`     | directory holding the ROMs; **unset → test skips**          |
| `MNEMOS_C64_BOOT_FRAMES` | frames to render before hashing (default `200`)             |
| `MNEMOS_C64_BOOT_SHA256` | the golden hash to assert; unset → the hash is printed only |

With ROMs present but no golden hash, the test still asserts the boot is
**deterministic** (two cold boots hash identically) and produces **visible
output** (the raster is not a uniform blank), and prints the computed hash so you
can lock it in.

### Recording and locking the golden

```pwsh
$env:MNEMOS_C64_ROM_DIR = "C:/path/to/c64-roms"
ctest --preset windows-msvc-debug -R c64_basic_boot_test --output-on-failure
# read the printed "boot framebuffer sha256 = ..." line, then:
$env:MNEMOS_C64_BOOT_SHA256 = "<that hash>"
ctest --preset windows-msvc-debug -R c64_basic_boot_test
```

The same hash is produced by the headless CLI, which is the easiest way to
eyeball the boot screen while you are at it:

```pwsh
mnemos_runtime_cli --manifest src/manifests/c64/c64.pal.toml `
  --rom-dir C:/path/to/c64-roms --frames 200 --dump-hash
```
