# Mnemos Player

`mnemos_player` is the windowed frontend. It is system-agnostic: it drives any
`mnemos::frontend_sdk::player_system` implementation, so the same binary boots
any wired-in system. The system family is picked from the ROM file extension.

Currently wired:

- **Genesis / Mega Drive** - `.md`, `.gen`, `.smd`, `.bin`, `.68k`
- **Sega Master System / Game Gear** - `.sms`, `.sg`, `.gg`
- **Commodore 64, ZX Spectrum, NES, MSX, MSX2, Amiga 1000/500/500+/600/2000**
- **Sega CD, Sega 32X, Irem M72, Taito F2, Capcom CPS1/CPS2**

## Stack

- Window / GPU / input / audio: SDL3 (fetched via
  `cmake/modules/MnemosFetchSDL3.cmake`, pinned, built static).
- Render: SDL_GPU explicit pipeline. Each adapter's RGBA framebuffer is
  uploaded to a streaming texture and presented at integer letterbox scale.
- Audio: `SDL_AudioStream` consumes the mixed stereo s16 drained from the
  active adapter each frame.
- Input: keyboard + `SDL_Gamepad` hot-plug, OR'd into a system-agnostic
  `controller_state` that each adapter maps onto its hardware.

## Keys

- `ESC` quit, `F11` fullscreen toggle, `P` pause, `F12` framebuffer dump.
- For systems with mouse input, click inside the emulation view or press
  `Ctrl+M` to capture the host mouse, hide the host pointer, and feed relative
  motion to the emulated mouse. Press `ESC` once to release capture; press it
  again to quit.
- Keyboard: arrows = dpad, `Z`/`X`/`C` = A/B/C, `A`/`S`/`D` = X/Y/Z,
  `Enter` or `1` = Start, `Backspace` or `5` = Select / arcade coin,
  `Shift` or `F2` = Mode / arcade service.

## Headless Input

- `--press <button>@<frame>[+duration]` scripts port 1 for screenshots,
  animations, asset extraction, and rendered-audio exports.
- `--press pN:<button>@<frame>[+duration]` targets a specific 1-based input
  port, e.g. `--press p2:select@120+4` for player-2 coin on arcade boards.

## Computer Keyboard Layouts

- `--keyboard-layout <token>` selects the host physical keyboard layout for
  computer adapters that expose a keyboard port.
- Amiga 1000/500/500+/600/2000 accepts `us`, `de`, `qwertz`, `fr`, `azerty`,
  `en-gb`, `es`, `it`, `sv`, `fi`, `dk`, and `nb-no`.
  `MNEMOS_AMIGA500_KEYBOARD_LAYOUT` remains the fallback when the CLI option is
  absent.
- Amiga Kickstart ROMs can be supplied explicitly per launch with
  `--amiga-kickstart <path>`. This option overrides environment variables and
  shared BIOS directory discovery, which is useful when a compatibility probe
  needs a non-default Kickstart for a given model. Without the CLI option, set
  `MNEMOS_AMIGA1000_KICKSTART`, `MNEMOS_AMIGA500_KICKSTART`,
  `MNEMOS_AMIGA500PLUS_KICKSTART`, `MNEMOS_AMIGA600_KICKSTART`, or
  `MNEMOS_AMIGA2000_KICKSTART`. The player also accepts matching `*_BIOS`
  aliases, or a shared `MNEMOS_AMIGA_BIOS_DIR` / `MNEMOS_AMIGA_KICKSTART_DIR`
  containing model-appropriate names such as `Kickstart 1.0.rom` for A1000,
  `Kickstart 1.3.rom` for A500/A2000, and `Kickstart 2.0.rom` for A500+/A600.
  A1000 currently supports resident Kickstart 1.0 ROM dumps; the real bootstrap
  ROM plus Kickstart-disk WCS loading path is still a follow-up.
- Amiga ADF media may be supplied directly, as `.adz` / `.adf.gz`, in direct
  ZIP or TAR archives, as `.tar.gz` / `.tgz`, in tool-backed `.7z`, `.rar`,
  `.lha`, or `.lzh` archives when the platform `tar`/libarchive command can
  inspect that format, or in one of the common outer ZIP wrappers that contain
  nested per-disk ZIPs.
  When direct or nested archives expose a complete `(Disk N of M)` sequence, the
  player mounts it in disk order. Multiple `--rom`, `-r`, or `--disk` paths are
  aggregated in command-line order, so separate Boot/Extras archives or
  per-disk game archives mount as one disk set. The current floppy path accepts
  standard 901,120-byte DD ADF images and supported `UAE-1ADF` extended ADF
  images with normal AmigaDOS tracks or raw MFM tracks. IPF floppy images are
  decoded through the SPS/CAPS Access API when the user supplies the CAPSImg
  library separately. Set `MNEMOS_AMIGA_CAPSIMG_DLL` or `MNEMOS_CAPSIMG_DLL` to
  the library path, place `CAPSImg.dll` / `CAPSImg_x64.dll` in
  `MNEMOS_AMIGA_BIOS_DIR`, or put the library beside `mnemos_player`. Mnemos
  does not vendor or redistribute the SPS library. HDF/HDZ hard-drive images are
  recognized as Amiga media classes, but still fail with explicit
  unsupported-media errors until the hard-drive controller path is implemented.
  Extended ADF overtrack payloads are also rejected as unsupported media.
  `scripts/amiga/run-corpus-smoke.ps1 -RomDir <path>` scans only direct ADF/ADZ
  files and supported archives that expose direct ADF/ADZ entries or supported
  nested ZIP candidates. Complete filename-marked disk sets such as
  `(Disk 1 of 3)` / `(Disk 2 of 3)` / `(Disk 3 of 3)` are launched together.
  The runner also infers complete contiguous sequences such as
  `Game_disk1` / `Game_disk2` or `Game_1` / `Game_2` when every disk from 1
  through the highest number is present with no duplicates, so directory
  scans exercise the same multi-disk path as repeated `--rom`/`--disk`
  launches. Use `-ListSets` to inspect the grouped launch plan without
  running the emulator. Use `-MaxSets <n>` with `-StartAfter <path-or-name>` to
  walk a large local corpus in deterministic slices. Add `-MinimumHeadlessFps
  <fps>` to fail the smoke when a launch falls below the requested headless
  throughput. Add `-RequireDiskProgress` with optional `-MinimumDiskCylinder`
  to require the Amiga board register dump to show disk progress through a
  nonzero disk DMA pointer, active DMA remainder, or observed head movement;
  optionally require head movement beyond a requested cylinder. Add
  `-RejectKickstartPrompt` when promoting compatibility rows so known insert-disk
  prompt frames do not count as operational software. Add `-RejectFlatFrame`,
  `-MinimumUniqueColors`, or `-MaximumDominantColorRatio` to keep black, white,
  or otherwise flat loading frames from being mistaken for visual proof. Use
  `-AllowBlackFrame` only for non-visual probes such as audio-only gates. Add
  `-RequireRenderedAudio`
  with optional `-AudioFrames`, `-AudioPress`,
  `-MinimumAudioFramesWithSignal`, and `-MinimumAudioPeakAbs` to gate titles
  that should produce Paula output after scripted input. Per-run timing and
  Kickstart identity, optional rendered-audio metrics, screenshot hashes,
  screenshot color-diversity metrics, and audio artifact hashes are written to
  `build/scratch/amiga-corpus/summary.csv`. Use
  `-ExpectedSummary <csv-or-json>` to compare current screenshot/audio hashes
  against a prior or reference summary. Summary matching includes the resolved
  Kickstart route when present; add `-RequireExpectedRows` when every current
  row must have a baseline.
- Amiga 1000 defaults to an OCS/256 KiB NTSC profile suitable for resident
  Kickstart 1.0 testing; use `--region pal` for PAL hardware routes.
- Amiga 2000 defaults to the base OCS/512 KiB profile. Use
  `--amiga-model ecs-1m` or `MNEMOS_AMIGA2000_MODEL=ecs-1m` for an upgraded
  ECS / 1 MiB Kickstart 2.x-style configuration. Add Fast RAM with a combined
  token such as `--amiga-model ecs-1m+fast-ram=2m`; the RAM is exposed as a
  Zorro II autoconfig memory board and becomes CPU-visible after Kickstart
  assigns it.
