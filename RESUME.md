# MSX / MSX2 Resume Handoff

Generated: 2026-06-26 23:03 America/Chicago
Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote: `origin/feature/msx2`

This handoff exists because the original Codex session ran for roughly 30
hours and hit practical context-window limits. Continue from this file instead
of reconstructing the full chat history.

## Start Here

Use the MSX2 worktree, not the root checkout:

```powershell
Set-Location C:\dev\emu\Mnemos-msx2
git fetch origin
git status --short --branch --untracked-files=all
git log -8 --oneline --decorate
Get-Content .\CONSTITUTION.md
Get-Content .\README.md
Get-Content .\RESUME.md
```

The root checkout at `C:\dev\emu\Mnemos` was on an unrelated branch during the
handoff work. Continue MSX/MSX2 work from `C:\dev\emu\Mnemos-msx2`.

## User Contract

- Implement both MSX and MSX2; they share common manifest, adapter, player
  launch, mapper, video, and golden-test surfaces.
- Do not claim "100% working" until representative real-ROM/media validation
  proves it. Current state is partial.
- A blank Mnemos Player window is not proof. Launch with explicit `--system`
  and `--rom`, or use the data-gated smoke runner.
- Preserve the requested branch/worktree name: `feature/msx2`.
- Keep ROMs, firmware, screenshots, logs, and build outputs out of Git.

## BIOS / ROM Inputs

The user's C-BIOS root is:

```text
D:\emu\msx\bios
```

Known firmware files used by this worktree:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_main_msx2_br.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
```

The local cartridge corpus used in this session is:

```text
D:\emu\msx\MSX files [ROM]
```

All runtime artifacts should stay under `build\`, preferably
`build\scratch\...`.

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- MSX and MSX2 C-BIOS boot paths run through golden tests and explicit player
  launches when invoked with system/media arguments.
- MSX and MSX2 share RAM-size profile semantics through the manifest, player
  launch, adapter, smoke script, and golden-test paths.
- `3D Pool [cas2rom64ks]` now auto-detects as ASCII16 through the shared mapper
  resolver and boots through both MSX and MSX2.
- `a_test2 [Arabic MSX].rom` is profiled for MSX2 BR C-BIOS instead of being
  treated as a V9938 renderer failure.
- `abbaye_v1.1.rom` now resolves its strong ASCII8 loader-write signature
  before a lower-page self-modifying-code hit can misclassify it as Generic8.
- Bounded real-ROM smoke windows have passed through skip 95:
  - `-SkipRoms 12 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 24 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 36 -MaxRoms 12`: `26/26` passed.
  - `-SkipRoms 48 -MaxRoms 12`: `26/26` passed after profiling
    `a_test2 [Arabic MSX].rom` against BR C-BIOS.
  - `-SkipRoms 60 -MaxRoms 12`: `26/26` passed after fixing the shared
    ASCII8-vs-Generic8 mapper classifier.
  - `-SkipRoms 72 -MaxRoms 12`: `25/25` passed; `AKUPRO.rom` is skipped on
    MSX by profile as MSX2-only and validated on MSX2.
  - `-SkipRoms 84 -MaxRoms 12`: `26/26` passed.

Known gaps:

- Continue the bounded corpus at `-SkipRoms 96 -MaxRoms 12`.
- This is not yet a representative compatibility matrix.
- Earlier notes included Bosconia staying on the C-BIOS logo and MSX2 Bestial
  Warrior color fidelity suspicion; those still need confirmation in later
  slices.

## Latest Passing Commit Before Handoff

The branch was clean and pushed at:

```text
6ded9c47 (HEAD -> feature/msx2, origin/feature/msx2) Profile Arabic MSX2 regional BIOS boot
```

The current worktree should include a newer commit that fixes `abbaye_v1.1`
mapper detection and updates this file.

## Latest Change

The latest source change is in:

```text
src/manifests/common/msx_cartridge_mapper.cpp
src/manifests/common/tests/msx_cartridge_mapper_test.cpp
```

`abbaye_v1.1.rom` carries three strong ASCII8 register writes at `$6800`,
`$7000`, and `$7800`, but also writes to `$40C1` as self-modifying code. The
old classifier treated that lower-page code patch as Generic8 evidence and
chose Generic8 before considering the stronger ASCII8 loader signature. The
new classifier lets `ascii8_count >= 3` win before the Generic8 path, and the
regression test captures the same false-positive shape.

## Latest Validation: Skip 60 Window

Command run:

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
  -SkipRoms 60 `
  -MaxRoms 12 `
  -RequireData
```

Result:

```text
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-231011-313-3300\summary.json
```

Important fixed-case hashes:

```text
MSX firmware baseline:
697fe93903980d26d6ef37fc76b0511c8a76656ebdc70f7be616c4dd7bac836a

MSX abbaye_v1.1:
2089850df8c39e4ffac5a7f4d095b5dc46357e513cfb4b95fd13a2ada7dc63f2

MSX2 firmware baseline:
8572e9f4e74913c1a5d3de86da650c0a46ab5ec0643b2d6e5f80bf0c3fd5e1bb

MSX2 abbaye_v1.1:
69c453bc568503eb6ce377a406183182b131b620613a82ef737f4b849cbea2d5
```

Direct post-fix Catch2 proof for `abbaye_v1.1.rom`:

```text
resolved cartridge mapper: ASCII8
boot framebuffer sha256: 2089850df8c39e4ffac5a7f4d095b5dc46357e513cfb4b95fd13a2ada7dc63f2
diagnostic log: build\scratch\msx-abbaye-triage\20260626-2305\msx-abbaye-catch2-s-after-fix.txt
```

Explicit player proof:

```text
command included: --system msx --rom "D:\emu\msx\MSX files [ROM]\abbaye_v1.1.rom"
output: build\scratch\msx-abbaye-proof\20260626-2310\abbaye-msx.ppm
PPM SHA256: 893782F6AE8C5DD17DB8CF0E47CF084B5C2D9C1022E8117157F00BD19AC7E0AF
sample_unique_bytes=4
```

## Fixed Failure Details

ROM:

```text
D:\emu\msx\MSX files [ROM]\abbaye_v1.1.rom
Size: 262144
SHA256: A8F52BE42A9731DF93D68B78F1BA00F03BF2B04582CA532359B27AD8D3EF25B9
```

Observed:

- Before the fix, MSX automatic mapping resolved to Generic8 and the framebuffer
  stayed equal to the C-BIOS firmware baseline after 3600 frames.
- Explicit mapper probes showed `ascii8` and `ascii16` both escaped the
  baseline, while `generic8` reproduced the failure.
- After the shared classifier fix, automatic mapping resolves to ASCII8 and the
  MSX framebuffer differs from the firmware baseline.
- The ROM header starts with `41 42 AE 40`, giving an init vector around
  `$40AE`.
- Early bytes include loader writes to `$6800`, `$7000`, and `$7800`, plus a
  code patch at `$40C1`; the code patch caused the old Generic8 false positive.

## Suggested Next Implementation Slice

Run the next bounded MSX/MSX2 ROM corpus smoke:

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
  -SkipRoms 96 `
  -MaxRoms 12 `
  -RequireData
```

## Build / Test Commands

Windows MSVC build/test must run from a Visual Studio developer environment.
This command form has worked reliably from PowerShell:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test'
```

Focused tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test|mnemos_manifests_common_test" --output-on-failure'
```

If Ninja/MSVC hits a transient `LNK1104` archive race, retry the build with:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --parallel 1'
```

## Validation Already Completed

Focused RAM-size/profile build:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_apps_player_system_launch_test mnemos_msx_boot_test mnemos_apps_player_msx_adapter_test mnemos_manifests_msx_test'
```

Result:

```text
passed
```

Focused RAM-size/profile tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug -R "mnemos_apps_player_system_launch_test|mnemos_msx_boot_test|mnemos_apps_player_msx_adapter_test|mnemos_manifests_msx_test" --output-on-failure'
```

Result:

```text
100% tests passed, 0 tests failed out of 4
```

Mapper-focused build and tests:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_common_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_common_test|mnemos_msx_boot_test" --output-on-failure'
```

Result:

```text
100% tests passed, 0 tests failed out of 2
```

Real-data `3D Pool [cas2rom64ks]` MSX/MSX2 smoke:

```text
MSX/MSX2 boot smoke: 4/4 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-221758-220-55992\summary.json
MSX ROM hash: 28e81fcc84603a60d9c5fe8489a9292fb383bf85ce304f0622fe1906bb35894a
MSX2 ROM hash: 39285647f88ac0fac02e87cc039f1e113246f52079eeea1da5c72f0afe874b0f
```

Explicit player proof for `3D Pool [cas2rom64ks]`:

```text
MSX screenshot: build\scratch\msx-cas2rom-proof\20260626-221758\3d-pool-cas2rom-msx.ppm
MSX screenshot SHA256: 73C06DC60666EAADDDBFD2F2A5A28FBA33001CB8C9C9D6562D4DF5FA1A10B8AF
MSX2 screenshot: build\scratch\msx-cas2rom-proof\20260626-221758\3d-pool-cas2rom-msx2.ppm
MSX2 screenshot SHA256: 4977EC40C4D19E041EDB1282C6083ADADEF1EB05A0E2A285DF3FEB9BFE1B0893
```

Arabic MSX2 BR C-BIOS proof:

```text
MSX/MSX2 boot smoke: 2/2 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224621-597-43940\summary.json
rom hash: 7855330785cbb9bcd8ba95149af8eef1fa67f035abbdc88c61e38fc0b6d27d9d
```

Profile-driven Arabic MSX2 proof:

```text
MSX/MSX2 boot smoke: 2/2 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224755-972-58776\summary.json
```

Explicit player proof for Arabic MSX2:

```text
mnemos_player wrote build\scratch\msx2-a-test2-proof\20260626-225405\a-test2-msx2-br.ppm
PPM SHA256: B4DD3D7053F38E6729A808858CC703CEFC063E856A4AE1CE89738F5E95CAAC19
PPM non_uniform=True
```

Latest passing corpus windows:

```text
-SkipRoms 24 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-222804-481-14904\summary.json

-SkipRoms 36 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-223029-317-62816\summary.json

-SkipRoms 48 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-224915-455-69380\summary.json

-SkipRoms 60 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-231011-313-3300\summary.json

-SkipRoms 72 -MaxRoms 12:
MSX/MSX2 boot smoke: 25/25 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-231514-754-50788\summary.json

-SkipRoms 84 -MaxRoms 12:
MSX/MSX2 boot smoke: 26/26 passed
summary: C:\dev\emu\Mnemos-msx2\build\scratch\msx-boot\20260626-231740-241-85428\summary.json
```

## Handoff Notes

- Use `rg` first for searches and batch reads where possible.
- Use `apply_patch` for manual source edits.
- Do not vendor emulator source or ROM data.
- Do not treat a successful Catch2 process exit as proof when the smoke runner
  reports a firmware-baseline framebuffer.
- Preserve both MSX and MSX2 behavior in every shared fix.
- If the next framebuffer issue is fixed, continue bounded corpus validation
  rather than claiming broad compatibility from one ROM.
