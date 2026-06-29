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

## CPS2 Corpus Frame, Audio, And EEPROM Hashes

`tests/golden/cps2_frame_hash_baseline.csv` pins the default save/load
screenshot, rendered-audio probe, DL-1425 QSound register probe, and 93C46
battery image for the local CPS2 catalog sweep. It stores zip names, dimensions,
RGB hash algorithm, RGB hash, nonzero pixel counts, WAV stream shape, PCM hash,
audio amplitude counts, raw first/last nonzero rendered-audio positions, thresholded
first/last significant rendered-audio sample/frame positions,
QSound port/register activity, last QSound register commit, PCM/ADPCM
programming counters, ADPCM voice-configuration categories, a programmed-silent
flag, EEPROM byte count, EEPROM hash, and non-erased byte counts only; ROMs,
keys, screenshots, WAVs, EEPROM dumps, register dumps, and logs remain local
under `build/scratch/`.

Most rows use the 600-frame screenshot/state gate. `dstlk.zip` intentionally
runs the save/load and default audio probes for 1200 frames because its
EEPROM/QSound self-initialization remains black past the 600-frame checkpoint
and reaches a stable visible attract frame by 1200. `hsf2.zip` keeps the
600-frame screenshot/state gate but records a 6040-frame audio-only probe,
matching the focused HSF2 QSound oracle window where significant rendered audio
starts after frame 1198; the corpus runner applies that HSF2 audio window
automatically for the committed baseline.

Point `MNEMOS_CPS2_SET_DIR` at a directory of CPS2 zip/key sets, build
`mnemos_player`, then run:

```pwsh
.\scripts\run-data-gated-tests.ps1 -BuildDir build/windows-msvc-debug
```

When `MNEMOS_CPS2_SET_DIR` is set, the runner automatically compares against the
committed baseline. Set `MNEMOS_CPS2_FRAME_HASH_BASELINE` to another CSV when
testing an intentional rendering change or a local candidate baseline.
Focused runs using `-Rom`, `-OnlySets`, `-SkipSets`, `-StartAfter`, or `-MaxSets`
compare only the selected rows and do not fail just because other baseline rows
were not part of that focused proof. Unfiltered `MNEMOS_CPS2_SET_DIR` sweeps
still require every committed baseline row to be present.

Some CPS2 games do not program audible QSound output inside the default
600-frame screenshot/state window. Use a longer audio-only proof window and the
Emu-derived coin/start/fire input cadence when triaging those rows. By default,
`-GameplayInput` affects only the rendered-audio proof and leaves the 600-frame
save/load screenshot on the stable baseline input cadence; add
`-GameplayPlayers 1` when comparing against Emu's default one-player
`--auto-start` probe, `-GameplayRepeat` to repeat the Emu-style coin/start pulses
every 300 frames, `-AudioStateProbe` to run a second final-state
screenshot/register pass and record `audio_qsound_*` counters, or
`-GameplaySaveInput` only when you intentionally want gameplay inputs in the
save-state screenshot. The runner keeps the raw first/last nonzero PCM positions and
also records the first/last sample/frame whose absolute value reaches
`-AudioSignificantThreshold` (default 64), which avoids treating tiny QSound
echo/filter residue as gameplay audio. Alternate audio
windows, gameplay inputs, thresholds, or set filters are evidence-collection runs
unless you explicitly pass a matching
`MNEMOS_CPS2_FRAME_HASH_BASELINE`:

```pwsh
.\scripts\cps2\run-corpus-smoke.ps1 `
  -Rom D:\emu\capcom\cps2\1944_mn.zip `
  -Frames 600 -AudioFrames 2500 -GameplayInput
```

The corpus runner writes `summary.json` and `frame_hashes.csv` after every set,
so long sweeps remain useful if interrupted. Resume or focus them with
`-OnlySets`, `-SkipSets`, and `-StartAfter`, or with the wrapper environment
variables `MNEMOS_CPS2_ONLY_SETS`, `MNEMOS_CPS2_SKIP_SETS`,
`MNEMOS_CPS2_START_AFTER`, `MNEMOS_CPS2_AUDIO_STATE_PROBE`,
`MNEMOS_CPS2_AUDIO_SIGNIFICANT_THRESHOLD`, `MNEMOS_CPS2_GAMEPLAY_PLAYERS`,
`MNEMOS_CPS2_GAMEPLAY_REPEAT`, and `MNEMOS_CPS2_GAMEPLAY_SAVE_INPUTS`.
