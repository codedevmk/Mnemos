# MSX / MSX2 Resume Handoff

Generated: 2026-06-27
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote tracking branch: `origin/feature/msx2`
Pushed base before this handoff checkpoint: `f97a65fc Expose V9938 palette sidecar`

## Resume Point

Start in the dedicated MSX/MSX2 worktree:

```powershell
Set-Location C:\dev\emu\Mnemos-msx2
git status --short --branch --untracked-files=all
git log -5 --oneline --decorate
Get-Content .\CONSTITUTION.md
Get-Content .\README.md
Get-Content .\RESUME.md
```

At the time this handoff checkpoint started, `HEAD` and `origin/feature/msx2`
both pointed at `f97a65fc`. This checkpoint commit adds the Arabic `1 2 3`
profile entries, refreshes this handoff, and should be the new
`origin/feature/msx2` tip after push. The root checkout `C:\dev\emu\Mnemos` is
not the MSX/MSX2 worktree.

The current request is to implement both MSX and MSX2 because they share common
components. Do not claim a "100% working" state until it is proven with explicit
ROM-backed player launches using both `--system` and `--rom`. A blank Mnemos
Player window is not proof.

## Local Firmware

The user's C-BIOS root is:

```text
D:\emu\msx\bios
```

Known C-BIOS paths used for prior proof:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
```

Useful environment setup:

```powershell
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_REGION='ntsc'
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_MSX2_REGION='ntsc'
```

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- MSX and MSX2 C-BIOS boot paths run through the player when invoked with
  explicit `--system` and `--rom`.
- Several MSX/MSX2 cartridges render visible, nonblank game/title output.
- V9938 reset palette constants are fixed and covered by tests.
- V9938 live palette RAM is exposed as a stable 32-byte introspection memory
  view, so player screenshots now emit `.v9938.palette.bin` sidecars.
- MSX2 now has the same lower-page C-BIOS-style plain 32 KiB ROM handoff
  behavior that was already implemented for MSX.
- The Arabic `1 2 3` ROM now has explicit MSX and MSX2 profile entries with
  3600-frame boot hashes, avoiding the old 600-frame false failure.

Known gaps:

- MSX/MSX2 are not proven across a representative compatibility matrix.
- MSX2 `Bestial Warrior` renders title art and now dumps the live V9938
  palette; color fidelity is still suspect and needs separate triage.
- Local `Bosconia.rom` still stays on the C-BIOS logo.
- The local Bosconia dump appears to be a 16 KiB file while current MAME
  software-list metadata found during prior work lists Star Destroyer
  Bosconian entries as 32 KiB. Do not loosen global 16 KiB mirroring behavior
  just to force this local dump to run.

## Latest Implemented Changes

Latest handoff checkpoint:

```text
Profile Arabic MSX boot proof
```

Files changed:

```text
tests/golden/msx_rom_profiles.json
RESUME.md
```

Behavior added:

- Added MSX and MSX2 profile entries for
  `1 2 3 (Al Alamiah) (1986) [Arabic MSX] [a].rom`.
- Both entries use the same ROM SHA256
  `87cfdbe3f4c335ef723a7e0aacfabdeaab894e18aa40d2a8292954524ef2ab98`.
- Both entries use `mapper=plain` and `boot_frames=3600`.
- MSX expected framebuffer SHA256:
  `7120ea62709b765f235cdb681fa94ce76d0091a9564ccf5945c42d068091f689`.
- MSX2 expected framebuffer SHA256:
  `d6c8dd5aba21d95924aa28573796d1f6ca7748cf36e0369cbeaf189695b3b21b`.

Previous implementation slice:

```text
Expose V9938 palette bytes through introspection and player sidecars
```

Files changed:

```text
src/chips/video/v9938/v9938.hpp
src/chips/video/v9938/v9938.cpp
src/chips/video/v9938/tests/v9938_test.cpp
RESUME.md
```

Behavior added:

- Added a derived `palette_bytes_` mirror to V9938 with 16 little-endian RGB333
  words, 32 bytes total.
- Synced the byte mirror after reset, after each committed `palette_write`, and
  after `load_state`.
- Added a fifth V9938 introspection memory view named `palette`.
- Updated the V9938 introspection regression to assert the new view, size, and
  little-endian byte order for reset and written palette entries.

Previous pushed implementation commit:

```text
fdede017 Add MSX2 lower plain ROM handoff
```

Files changed by that implementation:

```text
src/manifests/msx2/msx2_system.hpp
src/manifests/msx2/msx2_system.cpp
src/manifests/msx2/tests/msx2_system_test.cpp
RESUME.md
```

Behavior added:

- Added `cartridge_lower_handoff` and `cartridge2_lower_handoff` to MSX2 system
  state.
- Made MSX2 plain 32 KiB cartridge handoff support lower and upper windows.
- Reordered MSX2 `read_slot()` so lower-page handoff is checked before BIOS/RAM
  dispatch.
- Gated lower handoff through
  `common::msx_plain_32k_lower_handoff_required`, matching the MSX path.
- Added a regression proving the lower handoff is cartridge-specific and only
  becomes visible after the latch is enabled.

## Latest Validation

Validation for this handoff checkpoint:

```powershell
Get-Content -Raw tests\golden\msx_rom_profiles.json | ConvertFrom-Json | Out-Null

$rom='D:\emu\msx\MSX files [ROM]\1 2 3 (Al Alamiah) (1986) [Arabic MSX] [a].rom'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -MsxBios 'D:\emu\msx\bios\cbios\cbios_main_msx1.rom' `
  -MsxLogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx1.rom' `
  -MsxRom $rom `
  -MsxRegion 'ntsc' `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2Rom $rom `
  -Msx2ExpandedSlots '8' `
  -Msx2SubSlot '3.0' `
  -Msx2RamSlot '3.2' `
  -Msx2RamSize '512K' `
  -Msx2Region 'ntsc' `
  -RomProfileManifest 'tests/golden/msx_rom_profiles.json' `
  -Frames 600 `
  -RequireData
```

Result:

```text
JSON manifest parsed successfully
targeted profile-driven smoke passed: 4/4
summary: build\scratch\msx-boot\20260626-214407-685-21288\summary.json
```

Validation previously completed for `fdede017`:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx_test|mnemos_manifests_msx2_test" --output-on-failure'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_player'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "msx|MSX|tms9918a|v9938|mnemos_apps_player_system_launch_test|mnemos_apps_player_capability_summary_test" --output-on-failure'
```

Result:

```text
mnemos_manifests_msx_test passed
mnemos_manifests_msx2_test passed
focused MSX/MSX2/VDP/player slice passed: 12/12 tests
```

Validation for the V9938 palette-sidecar slice:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|msx|MSX|mnemos_apps_player_system_launch_test|mnemos_apps_player_capability_summary_test" --output-on-failure'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_player'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug --output-on-failure'
```

Results:

```text
focused V9938/MSX/MSX2/player slice passed: 11/11 tests
full Windows preset build passed
full Windows preset ctest passed: 175/175 configured tests, expected skips only
```

## Explicit Player Proof

Latest Arabic `1 2 3` proof:

```text
ROM: D:\emu\msx\MSX files [ROM]\1 2 3 (Al Alamiah) (1986) [Arabic MSX] [a].rom
Size: 32768
SHA256: 87cfdbe3f4c335ef723a7e0aacfabdeaab894e18aa40d2a8292954524ef2ab98
SHA1: a7c116e987f561aaa6199c3018433c431268ca86
Header at $0000: 41 42, entry $00F7
Header at $4000: no AB header
```

Observed cases:

```text
msx  1 2 3 (Al Alamiah) (1986) [Arabic MSX] [a].rom
     --mapper plain frames=3600 exit=0 renders visible Arabic/English title output, PC=$4058
     screenshot: build\scratch\msx-arabic-123-proof\20260627-current\arabic-123-msx-3600.png

msx2 1 2 3 (Al Alamiah) (1986) [Arabic MSX] [a].rom
     --mapper plain frames=3600 exit=0 renders non-uniform text/artifact output, PC=$1130
     screenshot: build\scratch\msx2-arabic-123-proof\20260627-current\arabic-123-msx2-3600.png
```

Prior explicit player proof lives under:

```text
build\scratch\msx2-lower-handoff-proof\20260627-verify
```

Observed cases:

```text
msx  Boing-b.rom      --mapper plain    frames=600  exit=0  renders game prompt path, PC=$4D9E
msx  Bosconia.rom     --mapper plain    frames=600  exit=0  still C-BIOS logo, PC=$108B
msx2 ASHGUINZ.rom     --mapper generic8 frames=600  exit=0  renders Zemina logo
msx2 Bestial Warrior (Dinamic) (1989) (Version ROM del juego, creada por Kabish).rom
     --mapper ascii8  frames=600  exit=0  renders title art, color fidelity still suspect
```

Important: use the full Bestial filename shown above. The shorter
`D:\emu\msx\MSX files [ROM]\Bestial Warrior.rom` was not readable in the
latest process.

Latest palette-sidecar proof:

```text
build\scratch\msx2-palette-sidecar-proof\20260627-v9938-palette
```

Command shape:

```powershell
build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system msx2 --rom 'D:\emu\msx\MSX files [ROM]\Bestial Warrior (Dinamic) (1989) (Version ROM del juego, creada por Kabish).rom' --mapper ascii8 --screenshot build\scratch\msx2-palette-sidecar-proof\20260627-v9938-palette\bestial-msx2.png --frames 600
```

Observed artifacts:

```text
bestial-msx2.png                                7208 bytes, renders title art
bestial-msx2.png.v9938.palette.bin               32 bytes
bestial-msx2.png.v9938.vram.bin              131072 bytes
bestial-msx2.png.v9938.expanded_vram.bin      65536 bytes
bestial-msx2.png.v9938.registers.bin             64 bytes
bestial-msx2.png.v9938.status.bin                10 bytes
```

Decoded palette sidecar:

```text
[$0000,$009F,$004F,$0000,$0049,$00DB,$006D,$00FF,$00F4,$00AB,$00EB,$00EA,$00F2,$00FA,$00E0,$00D0]
```

## Bosconia State

Local ROM identity captured previously:

```text
Path: D:\emu\msx\MSX files [ROM]\Bosconia.rom
Size: 16384
SHA256: 1a76d08f33bf927b0e2977c13e62d6f87ece4a2275965c9134a436b852952368
MD5: 7bcdaceb434822c8db0e967d79362d62
Header bytes: 41 42 0F 40 00 00 00 00 00 00 00 00 00 00 00 21
```

Fresh 2026-06-27 rerun:

```text
Path: D:\emu\msx\MSX files [ROM]\Bosconia.rom
Size: 16384
SHA1: b023073f86b10c90754218b14da3757f29b039fe
Player args: --system msx --rom <path> --mapper plain --frames 600
Observed: still C-BIOS/logo halt, PC=$108B
```

MAME `hash/msx1_cart.xml` was downloaded to:

```text
build\scratch\mame-msx1-cart.xml
```

The MAME entries found for Star Destroyer Bosconian were 32 KiB SHA1s:

```text
451cbb072011b26a7e0bc9931e67995a71f5fd6f
33ad6d1bf1fb816b232ea706d149327e363c7b21
0bf7b432dc132f9a7bc74106261bca07d5f99690
```

Trace notes from earlier proof:

- BIOS reaches Bosconia init at `$400F`.
- Bosconia copies `$6000-$7FFF` to `$8000-$9FFF` with `LDIR`.
- It reaches `$4082` and calls `$8AFB`.
- It does not return to `$4085`, so it never reaches the `$409A` hook install
  that should write `JP $40AE` into `$FD9A-$FD9C`.
- Later `$FD9A` hits are still C-BIOS default hook calls.
- Current evidence suggests Mnemos reaches mirrored ROM bytes from local file
  offset `$0AFB`, while work RAM at `$8AFB` contains copied bytes from local
  file offset `$2AFB`.

Current question for Bosconia:

```text
Is this local 16 KiB lower-page dump valid and expected to mirror into page 2,
or is it an unsupported/bad dump relative to known 32 KiB software-list entries?
```

Do not change shared 16 KiB cartridge mirroring without a hermetic regression.

## MSX2 Color State

The reset-palette bug is fixed, but Bestial MSX2 color fidelity is not proven.
The golden boot diagnostic showed this V9938 palette after the software/C-BIOS
path rewrote palette RAM:

```text
[$0000,$009F,$004F,$0000,$0049,$00DB,$016D,$01FF,$01F4,$01AB,$01EB,$01EA,$01F2,$01FA,$01E0,$01D0]
```

Useful next slice:

- Compare the dumped Bestial MSX2 V9938 palette and framebuffer against a
  trusted emulator/reference capture to isolate whether remaining color issues
  are palette writes, RGB333 expansion, register interpretation, or bitmap
  rendering.
- Continue Bosconia separately; do not conflate color work with the local
  16 KiB Bosconia dump/profile question.

## Canonical Commands

Read `CONSTITUTION.md` and `README.md` first in a fresh agent session.

Windows build and test:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug --output-on-failure'
```

Focused slice after MSX/MSX2 work:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "msx|MSX|tms9918a|v9938|mnemos_apps_player_system_launch_test|mnemos_apps_player_capability_summary_test" --output-on-failure'
```

If MSVC/Ninja hits a transient `LNK1104` archive race, retry the build with
`--parallel 1`.

## Do Not Do

- Do not use a blank player launch as proof.
- Do not launch without explicit `--system` and `--rom`.
- Do not claim global MSX/MSX2 completion from a few working ROMs.
- Do not treat green unit tests alone as "100% working".
- Do not commit ROMs, BIOS files, logs, screenshots, or build outputs.
- Do not edit the root `C:\dev\emu\Mnemos` checkout for this MSX/MSX2 work.
