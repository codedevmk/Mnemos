# Mnemos Player

`mnemos_player` is the windowed frontend. It is system-agnostic: it drives any
`mnemos::frontend_sdk::player_system` implementation, so the same binary boots
any wired-in system. The system family is picked from the ROM file extension.

Currently wired:

- **Genesis / Mega Drive** - `.md`, `.gen`, `.smd`, `.bin`, `.68k`
- **Sega Master System / Game Gear** - `.sms`, `.sg`, `.gg`
- **Commodore 64, ZX Spectrum, NES, MSX, MSX2, Amiga 500/500+/600/2000**
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
- Amiga 500/500+/600/2000 accepts `us`, `de`, `qwertz`, `fr`, `azerty`,
  `en-gb`, `es`, `it`, `sv`, `fi`, `dk`, and `nb-no`.
  `MNEMOS_AMIGA500_KEYBOARD_LAYOUT` remains the fallback when the CLI option is
  absent.
- Amiga Kickstart ROMs can be supplied explicitly with
  `MNEMOS_AMIGA500_KICKSTART`, `MNEMOS_AMIGA500PLUS_KICKSTART`,
  `MNEMOS_AMIGA600_KICKSTART`, or `MNEMOS_AMIGA2000_KICKSTART`. The player also
  accepts matching `*_BIOS` aliases, or a shared `MNEMOS_AMIGA_BIOS_DIR` /
  `MNEMOS_AMIGA_KICKSTART_DIR` containing model-appropriate names such as
  `Kickstart 1.3.rom` for A500 and `Kickstart 2.0.rom` for A500+/A600.
- Amiga ADF media may be supplied directly, as `.adz` / `.adf.gz`, in direct
  ZIP or TAR archives, as `.tar.gz` / `.tgz`, in tool-backed `.7z` archives
  when the platform `tar`/libarchive command can inspect them, or in one of the
  common outer ZIP wrappers that contain nested per-disk ZIPs.
  When direct or nested archives expose a complete `(Disk N of M)` sequence, the
  player mounts it in disk order. Multiple `--rom`, `-r`, or `--disk` paths are
  aggregated in command-line order, so separate Boot/Extras archives or
  per-disk game archives mount as one disk set. Only standard
  901,120-byte DD ADF images are accepted by the current floppy path; extended
  ADF, raw-track, and IPF images are rejected as unsupported media.
  `scripts/amiga/run-corpus-smoke.ps1 -RomDir <path>` scans only direct ADF/ADZ
  files and supported archives that expose direct ADF/ADZ entries or supported
  nested ZIP candidates.
- Amiga 2000 defaults to the base OCS/512 KiB profile. Use
  `--amiga-model ecs-1m` or `MNEMOS_AMIGA2000_MODEL=ecs-1m` for an upgraded
  ECS / 1 MiB Kickstart 2.x-style configuration. Add Fast RAM with a combined
  token such as `--amiga-model ecs-1m+fast-ram=2m`; the RAM is exposed as a
  Zorro II autoconfig memory board and becomes CPU-visible after Kickstart
  assigns it.
