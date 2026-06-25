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

Put the three C64 ROM images in one directory and point `MNEMOS_C64_ROM_DIR`
at it. The test loads each ROM by size: any 8 KiB file is tried for BASIC and
KERNAL, any 4 KiB file is tried for CHARGEN. See `src/manifests/c64/ROMS.md`
for acquisition.

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

## `mnemos_msx_boot_test`

Boots generated MSX and MSX2 BIOS programs in every run, renders fixed
framebuffers, and asserts stable hashes for a legal end-to-end CPU/bus/VDP
regression check. Real firmware cases are independently data-gated: unset
firmware variables skip that case, while local firmware asserts deterministic
output and, when a golden hash is supplied, exact framebuffer parity.

### Environment variables

| Variable                  | Effect                                                       |
|---------------------------|-------------------------------------------------------------|
| `MNEMOS_MSX_BIOS`         | 32 KiB MSX BIOS image; **unset -> MSX case skips**          |
| `MNEMOS_MSX_ROM`          | optional cartridge image                                    |
| `MNEMOS_MSX_ROM2`         | optional second cartridge; alias `MNEMOS_MSX_CART2`         |
| `MNEMOS_MSX_DISK_ROM`     | optional disk interface ROM                                 |
| `MNEMOS_MSX_DSK`          | optional flat MSX DSK image                                 |
| `MNEMOS_MSX_CAS`          | optional MSX CAS tape image                                 |
| `MNEMOS_MSX_KANJI_ROM`    | optional Kanji ROM image                                    |
| `MNEMOS_MSX_MAPPER`       | optional cartridge mapper override                          |
| `MNEMOS_MSX_MAPPER2`      | optional second cartridge mapper override                   |
| `MNEMOS_MSX_EXPANDED_SLOTS`| expanded slots: numeric mask or comma list like `0,3`      |
| `MNEMOS_MSX_RAM_SLOT`     | RAM slot as `primary` or `primary.secondary`, e.g. `3.1`    |
| `MNEMOS_MSX_DISK_SLOT`    | disk ROM slot as `primary` or `primary.secondary`           |
| `MNEMOS_MSX_CART2_SLOT`   | second cartridge slot as `primary` or `primary.secondary`   |
| `MNEMOS_MSX_REGION`       | `ntsc` (default) or `pal`                                   |
| `MNEMOS_MSX_BOOT_KEYS`    | held boot keys: names like `return` or matrix `row.bit`     |
| `MNEMOS_MSX_BOOT_FRAMES`  | frames to render before hashing (default `200`)             |
| `MNEMOS_MSX_BOOT_SHA256`  | the MSX golden hash to assert; unset -> hash is printed     |
| `MNEMOS_MSX2_BIOS`        | MSX2 main BIOS image; may include packed sub/disk ROMs      |
| `MNEMOS_MSX2_FIRMWARE`    | packed main BIOS + sub-ROM; **unset and no BIOS -> skips**  |
| `MNEMOS_MSX2_SUB_ROM`     | MSX2 sub-ROM image; alias `MNEMOS_MSX2_SUBROM`              |
| `MNEMOS_MSX2_LOGO_ROM`    | optional C-BIOS style logo ROM at slot 0 `$8000-$BFFF`      |
| `MNEMOS_MSX2_ROM`         | optional cartridge image                                    |
| `MNEMOS_MSX2_ROM2`        | optional second cartridge; alias `MNEMOS_MSX2_CART2`        |
| `MNEMOS_MSX2_DISK_ROM`    | optional disk interface ROM; alias `MNEMOS_MSX2_DISKROM`    |
| `MNEMOS_MSX2_DSK`         | optional flat MSX DSK image                                 |
| `MNEMOS_MSX2_CAS`         | optional MSX CAS tape image                                 |
| `MNEMOS_MSX2_KANJI_ROM`   | optional Kanji ROM image                                    |
| `MNEMOS_MSX2_MAPPER`      | optional cartridge mapper override                          |
| `MNEMOS_MSX2_MAPPER2`     | optional second cartridge mapper override                   |
| `MNEMOS_MSX2_EXPANDED_SLOTS`| expanded slots: numeric mask or comma list like `0,3`     |
| `MNEMOS_MSX2_RAM_SLOT`    | RAM slot as `primary` or `primary.secondary`, e.g. `3.1`    |
| `MNEMOS_MSX2_SUB_SLOT`    | MSX2 sub-ROM slot as `primary` or `primary.secondary`       |
| `MNEMOS_MSX2_DISK_SLOT`   | disk ROM slot as `primary` or `primary.secondary`           |
| `MNEMOS_MSX2_CART2_SLOT`  | second cartridge slot as `primary` or `primary.secondary`   |
| `MNEMOS_MSX2_RAM_SIZE`    | mapper RAM size in bytes, or `K`/`M` suffix such as `128K`  |
| `MNEMOS_MSX2_REGION`      | `ntsc` (default) or `pal`                                   |
| `MNEMOS_MSX2_BOOT_KEYS`   | held boot keys: names like `return` or matrix `row.bit`     |
| `MNEMOS_MSX2_BOOT_FRAMES` | frames to render before hashing (default `200`)             |
| `MNEMOS_MSX2_BOOT_SHA256` | the MSX2 golden hash to assert; unset -> hash is printed    |
| `MNEMOS_MSX_CASE_MANIFEST`| optional JSON manifest for mixed MSX/MSX2 smoke cases       |
| `MNEMOS_MSX_ROM_PROFILE_MANIFEST` | optional JSON profiles for directory-scan ROM cases |
| `MNEMOS_MSX_PC_WATCH`     | optional PC trace diagnostic, e.g. `BFF0-C020`              |

With firmware present but no golden hash, each case still proves that the boot
path is deterministic across two cold starts and that the final framebuffer is
not a uniform blank raster.
The smoke runner also records each system's firmware-only framebuffer hash and
fails ROM/media cases that produce the same hash, since that means the artifact
did not visibly affect the boot path.
For cases that override firmware, slots, boot keys, or frame counts, the runner
uses a matching no-media firmware baseline for that same effective environment
instead of comparing against the first global firmware case.
Disk-image cases also require the matching disk-interface ROM (`MNEMOS_MSX_DISK_ROM`,
`MNEMOS_MSX2_DISK_ROM`, or a packed MSX2 firmware/BIOS image that includes it);
otherwise the smoke runner marks the case incomplete and `-RequireData` fails.
Boot keys are held for the whole boot window and are recorded in
`summary.json`; accepted values include `return`, `space`, cursor/control keys,
or raw matrix positions such as `7.7`.

C-BIOS MSX2 machine profiles can be represented through the slot overrides. The
published C-BIOS XML maps the sub-ROM to `3.0` and main RAM to `3.2`, so proof
runs for that profile should pass `-Msx2SubSlot 3.0 -Msx2RamSlot 3.2`.
Some Arabic MSX2 cartridges depend on firmware character-set metadata; validate
those against the regional C-BIOS main ROMs (`cbios_main_msx2_br.rom` or
`cbios_main_msx2_eu.rom`) rather than the generic or JP main ROM.
A compatibility profile with `-Msx2SubSlot 3.0 -Msx2RamSlot 3.3` remains useful
for older MSX1 cartridge loaders that rewrite only the expanded slot latch on
the same RAM subslot they expect.
Some local legacy ROM conversions need narrower per-ROM profiles rather than a
new global default. `BARBARIAN.rom` reaches its MSX2 menu with C-BIOS sub-ROM at
`3.1` and RAM at `3.0`; the default `3.0`/`3.2` layout leaves its copied loader
executing from an unmapped page.

### Smoke runner

`scripts/msx/run-boot-smoke.ps1` wraps the real-firmware cases and writes logs
plus `summary.json` under `build/scratch/msx-boot/<timestamp>`. It accepts the
same artifact paths as parameters or environment variables, can scan raw
`.rom`/`.mx1`/`.mx2` cartridge directories, and can load a mixed MSX/MSX2 JSON
case manifest through `-CaseManifest` or `MNEMOS_MSX_CASE_MANIFEST`. Directory
scans can also load a ROM profile manifest through `-RomProfileManifest` or
`MNEMOS_MSX_ROM_PROFILE_MANIFEST` to apply per-ROM boot keys, mapper overrides,
frame counts, slot overrides, or skip a cartridge for one machine when the same
corpus directory contains mixed MSX/MSX2-only titles. Add
`-RequireData` for proof runs that must fail instead of silently skipping when
no real firmware is configured; manifest cases must provide or inherit firmware
for that mode.
Directory scans ignore files smaller than 8 KiB because those are MSX binary
loaders or fragments, not standalone cartridge images.
Use `-SkipRoms` with `-MaxRoms` to validate large cartridge directories in
stable chunks.
Some cartridge title screens intentionally take longer than the default proof
window to become visible. `-RetryFrames <count>` reruns only failed non-firmware
media cases at a longer frame count and records the initial attempt plus retry
metadata in `summary.json`; a retry pass remains visible as a retry, not as a
clean first-pass result.
For CPU-control-flow triage, set `MNEMOS_MSX_PC_WATCH` to a hexadecimal range
such as `54E0-5510` or `BFF0-C020`; the harness records the first high-RAM
entry plus the watched PCs in the failing case log.

```pwsh
scripts/msx/run-boot-smoke.ps1 `
  -BuildDir build/windows-msvc-debug `
  -MsxBios C:/path/to/msx-bios.rom `
  -MsxKanjiRom C:/path/to/msx-kanji.rom `
  -Msx2Firmware C:/path/to/msx2-packed-firmware.rom `
  -MsxRomDir C:/path/to/msx-roms `
  -Msx2RomDir C:/path/to/msx2-roms `
  -MsxMapper ascii8 `
  -MsxMapper2 ascii16 `
  -MsxExpandedSlots 3 `
  -MsxRamSlot 3.0 `
  -MsxDiskSlot 3.1 `
  -MsxBootKeys return `
  -Msx2Mapper konami-scc `
  -Msx2Mapper2 ascii8 `
  -Msx2ExpandedSlots 0,3 `
  -Msx2RamSlot 3.1 `
  -Msx2SubSlot 3.0 `
  -Msx2RamSize 128K `
  -Msx2BootKeys return `
  -RetryFrames 3600 `
  -SkipRoms 25 `
  -MaxRoms 25
```

ROM profile manifests may be a root array or an object with a `profiles` array.
Profiles match auto-discovered directory ROMs by `system`, `name`/`filename`,
and/or ROM-file `sha256`; `skip` suppresses a machine-incompatible directory
case with a recorded console message. Use explicit `boot_sha256` or
`framebuffer_sha256` for framebuffer goldens so ROM identity hashes stay
unambiguous. The tracked local corpus profile set lives at
`tests/golden/msx_rom_profiles.json` and should be passed with
`-RomProfileManifest tests/golden/msx_rom_profiles.json` for proof chunks that
scan the mixed MSX/MSX2 ROM directory.
Profiles may also override firmware paths with `bios`, `firmware`, `sub_rom`,
`logo_rom`, `disk_rom`, and `kanji_rom`; relative firmware names resolve beside
the corresponding globally configured firmware path, so a profile can select a
sibling C-BIOS main ROM without hardcoding the full local BIOS directory.
The local mixed-corpus profile set also carries machine gating and mapper fixes
for cartridges whose directory filename alone is not enough: for example,
`ASHGUINZ.rom` is skipped for MSX1 and uses the MSX2 `generic8` mapper with a
longer 3600-frame boot window, while `BARBARIAN.rom` uses a C-BIOS MSX2
slot-layout override for its legacy loader.
Padded plain ROM images with their only `AB` cartridge header at file offset
`$4000` are mapped from that payload offset by the shared MSX cartridge loader;
the Arabic `Believe It or Not` dump in the local corpus uses that path for
MSX2, while the MSX1 C-BIOS path remains gated because it stays on the startup
banner. Kabish `cas2rom64ks` conversions are profiled with ASCII8 banking and a
3600-frame boot window when the auto-detected converter marker is not enough to
distinguish the working bank layout.
Plain 16 KiB cartridge payloads mirror into the upper cartridge window when the
ROM header points at an upper-page init vector, or when the same cartridge slot
is selected in both `$4000-$7FFF` and `$8000-$BFFF`. This keeps upper-page init
vectors such as `BOARDELO.rom` visible to C-BIOS without letting lower-page-only
headers masquerade as page-2 cartridges during firmware scans.
The local `boing.rom` dump is profiled out because it returns to the C-BIOS
cartridge-initialization diagnostic on both machines; `Boing-b.rom` remains in
the same corpus chunk as the standalone BOING boot proof.

```json
{
  "profiles": [
    {
      "system": "msx",
      "name": "ADONIS (1988)(MSX Magazine).rom",
      "sha256": "<rom file sha256>",
      "boot_keys": "space"
    },
    {
      "system": "msx",
      "name": "AKUPRO.rom",
      "sha256": "<rom file sha256>",
      "skip": true,
      "reason": "MSX2-only cartridge; validate through the MSX2 profile"
    },
    {
      "system": "msx",
      "name": "ASHGUINZ.rom",
      "sha256": "<rom file sha256>",
      "skip": true,
      "reason": "MSX2-only AshGuine cartridge; validate through the MSX2 Generic8 profile"
    },
    {
      "system": "msx2",
      "name": "ASHGUINZ.rom",
      "sha256": "<rom file sha256>",
      "mapper": "generic8",
      "boot_frames": 3600
    },
    {
      "system": "msx2",
      "name": "Anty.rom",
      "sha256": "<rom file sha256>",
      "bios": "cbios_main_msx2_eu.rom"
    }
  ]
}
```

The case manifest may be a root array or an object with a `cases` array. Paths
can be absolute or repository-relative. Each case inherits the matching global
MSX or MSX2 firmware/media settings, then overrides any fields it defines.

```json
{
  "cases": [
    {
      "system": "msx",
      "name": "ascii8-smoke",
      "bios": "C:/path/to/msx-bios.rom",
      "rom": "C:/path/to/cart.rom",
      "rom2": "C:/path/to/slot2.rom",
      "kanji_rom": "C:/path/to/msx-kanji.rom",
      "mapper": "ascii8",
      "mapper2": "ascii16",
      "expanded_slots": "3",
      "ram_slot": "3.0",
      "disk_slot": "3.1",
      "boot_keys": "return",
      "frames": 300,
      "sha256": "<framebuffer hash>"
    },
    {
      "system": "msx2",
      "name": "scc-smoke",
      "firmware": "C:/path/to/msx2-packed-firmware.rom",
      "rom": "C:/path/to/cart.rom",
      "cart2": "C:/path/to/slot2.rom",
      "mapper": "konami-scc",
      "mapper2": "ascii8",
      "expanded_slots": "0,3",
      "ram_slot": "3.1",
      "ram_size": "128K",
      "boot_keys": "return",
      "region": "ntsc"
    }
  ]
}
```
