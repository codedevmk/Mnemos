# MSX / MSX2 Resume Handoff

Generated: 2026-06-27
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote: `origin/feature/msx2`

## Resume Point

Start here:

```powershell
Set-Location C:\dev\emu\Mnemos-msx2
git status --short --branch --untracked-files=all
git log -5 --oneline --decorate
Get-Content .\CONSTITUTION.md
Get-Content .\README.md
Get-Content .\RESUME.md
```

The active MSX/MSX2 implementation worktree is `C:\dev\emu\Mnemos-msx2`, not
the root checkout `C:\dev\emu\Mnemos`. The current implementation commit is:

```text
7874e359 Share MSX RAM size profile path
```

This handoff file is committed after that implementation commit. After pulling,
`origin/feature/msx2` should contain both commits.

## User Constraints

- Implement both MSX and MSX2 because they share common manifest, adapter,
  player-launch, and golden-test surfaces.
- Do not call MSX/MSX2 "100% working" until real ROM/media coverage proves it.
- A blank Mnemos Player window is not proof. Use explicit `--system` and
  `--rom`, or the data-gated boot smoke runner.
- User C-BIOS root:

```text
D:\emu\msx\bios
```

Known C-BIOS paths:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
```

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- MSX and MSX2 C-BIOS boot paths run through tests and player launch when
  invoked with explicit system/media.
- A bounded mixed MSX/MSX2 cartridge smoke slice passed `18/18` with real
  local ROM data before the latest RAM-size change:
  `build\scratch\msx-boot\20260626-215046-345-38160\summary.json`.
- The latest MSX RAM-size profile path is built, tested, and real-data smoked.
- Player headless proof wrote a real screenshot for an MSX cartridge with
  explicit `--system msx --rom ... --screenshot ... --frames 600`.

Known gaps:

- This is not a representative compatibility matrix.
- Some local cartridges still need triage; earlier notes included Bosconia
  staying on the C-BIOS logo and MSX2 Bestial Warrior color fidelity suspicion.
- Do not lock hashes for trivial/small dumps blindly; earlier `1Kpong!` proof
  showed a visible "No enough memory" style behavior before this RAM-size slice.

## Latest Change

Commit `7874e359` shares RAM-size profile semantics across MSX and MSX2.

Files changed:

```text
scripts/msx/run-boot-smoke.ps1
scripts/run-data-gated-tests.ps1
src/apps/player/adapters/msx/msx_adapter.cpp
src/apps/player/system_launch.cpp
src/apps/player/tests/system_launch_test.cpp
src/manifests/msx/msx_system.cpp
src/manifests/msx/msx_system.hpp
tests/golden/README.md
tests/golden/msx_boot_test.cpp
```

Behavior added:

- `MNEMOS_MSX_RAM_SIZE` is now parsed for MSX player launch profiles.
- `-MsxRamSize` is now accepted by `scripts/msx/run-boot-smoke.ps1`.
- MSX ROM profile and manifest case entries can now use `ram_size` /
  `ramSize`, matching MSX2.
- MSX adapter maps requested byte sizes to 16 KiB RAM mapper segments, clamped
  from 64 KiB minimum to 4 MiB maximum.
- `msx_config::ram_mapper_segments` is now `std::size_t`, so the config can
  represent a 4 MiB / 256-segment mapper instead of truncating through
  `std::uint8_t`.

## Validation Completed

Focused build:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_apps_player_system_launch_test mnemos_msx_boot_test mnemos_apps_player_msx_adapter_test mnemos_manifests_msx_test'
```

Result: passed.

Focused tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_apps_player_system_launch_test|mnemos_msx_boot_test|mnemos_apps_player_msx_adapter_test|mnemos_manifests_msx_test" --output-on-failure'
```

Result:

```text
100% tests passed, 0 tests failed out of 4
```

Real-data MSX RAM-size smoke:

```powershell
$rom='D:\emu\msx\MSX files [ROM]\1Kpong! (Phoenix Software) (2005) (Version del juego en color azul).rom'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -MsxBios 'D:\emu\msx\bios\cbios\cbios_main_msx1.rom' `
  -MsxLogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx1.rom' `
  -MsxRom $rom `
  -MsxRamSize '256K' `
  -MsxRegion 'ntsc' `
  -Frames 600 `
  -RequireData
```

Result:

```text
MSX/MSX2 boot smoke: 2/2 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-215931-952-40668\summary.json
firmware hash: 697fe93903980d26d6ef37fc76b0511c8a76656ebdc70f7be616c4dd7bac836a
rom hash: 6fb3baa2b1faf1f7ad1c8519eaf25f02c4abeba3b50abd1caabc250167c59f16
```

Explicit player proof:

```powershell
$proofDir='build\scratch\msx-ram-size-proof\20260626-215931'
New-Item -ItemType Directory -Force -Path $proofDir | Out-Null
$rom='D:\emu\msx\MSX files [ROM]\1Kpong! (Phoenix Software) (2005) (Version del juego en color azul).rom'
$env:MNEMOS_MSX_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx1.rom'
$env:MNEMOS_MSX_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx1.rom'
$env:MNEMOS_MSX_RAM_SIZE='256K'
$env:MNEMOS_MSX_REGION='ntsc'
build\windows-msvc-debug\src\apps\player\mnemos_player.exe `
  --system msx `
  --rom $rom `
  --screenshot "$proofDir\1kpong-msx-256k.ppm" `
  --frames 600
```

Result:

```text
[mnemos_player] system: MSX  region: NTSC (auto-detected)
[mnemos_player] wrote build\scratch\msx-ram-size-proof\20260626-215931\1kpong-msx-256k.ppm (256x192 after 600 frames)
SHA256: 3E6FC788C604CAB29205ABAEEC4C4C3AC1880A625AA2D8C19CDCAAAC6B6913AF
```

Note: `mnemos_player --help` is not a CLI help path here; it starts the D3D12
frontend and timed out in this session. Use the explicit headless screenshot
command above for bounded player proof.

## Suggested Next Slice

Run a broader MSX/MSX2 ROM corpus smoke after this checkpoint, using the shared
profile manifest and C-BIOS paths:

```powershell
$romDir='D:\emu\msx\MSX files [ROM]'
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir 'build/windows-msvc-debug' `
  -MsxBios 'D:\emu\msx\bios\cbios\cbios_main_msx1.rom' `
  -MsxLogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx1.rom' `
  -MsxRomDir $romDir `
  -MsxRegion 'ntsc' `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2RomDir $romDir `
  -Msx2ExpandedSlots '8' `
  -Msx2SubSlot '3.0' `
  -Msx2RamSlot '3.2' `
  -Msx2RamSize '512K' `
  -Msx2Region 'ntsc' `
  -RomProfileManifest 'tests/golden/msx_rom_profiles.json' `
  -Frames 600 `
  -RetryFrames 3600 `
  -SkipRoms 12 `
  -MaxRoms 25 `
  -RequireData
```

Use the failures from that run to choose the next compatibility slice. Avoid
global behavior loosenings for one questionable local dump.
