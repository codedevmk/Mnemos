# MSX / MSX2 Resume Handoff

Updated: 2026-06-28 America/Chicago

Workspace: `C:\dev\emu\Mnemos-msx2`
Branch: `feature/msx2`
Remote: `origin/feature/msx2`
Prior implementation checkpoint before the latest MSX/MSX2 diagnostic pass:
`0e5b71f23d8a3d8a5d6ec0e55041b678117f6d77`.

The latest handoff commit should be the current `HEAD` after the next agent
runs `git log -1 --oneline --decorate`.

This file is the resume point for the MSX/MSX2 implementation thread. The
original Codex session ran for roughly 30 hours and should not be used as the
primary context source. Continue from the live worktree, this file, and current
executable evidence.

## Latest Checkpoint

2026-06-28 01:48 America/Chicago:

- Fixed the shared Z80 `EI`/NMI corner found in the next MSX/MSX2 review
  slice. `EI` now sets `IFF1/IFF2` immediately while `ei_pending_` only gates
  maskable IRQ recognition through the following instruction boundary. If an
  `/NMI` edge is pending at that boundary, the NMI service now consumes the
  pending `EI` delay before saving `IFF1` into `IFF2`, so `RETN` restores the
  correct enabled state instead of preserving a stale disabled `IFF2`.
- Added a focused regression in `src/chips/cpu/z80/tests/z80_test.cpp` for
  `EI`, simultaneous pending IRQ+NMI, NMI vectoring to `$0066`, and `RETN`
  restoring `IFF1/IFF2=true`.
- Updated the MSX1 `bean.rom` C-BIOS profile hash in
  `tests/golden/msx_rom_profiles.json`. The hardware-correct `EI` timing moves
  the 600-frame MSX1 visible attract state from
  `8ac76412f61dbbd32e99439bf7cd43a0cdcbc9efb3328828dcefd261909a33a2` to
  `690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a` when
  run with `cbios_logo_msx1.rom`; the no-logo direct MSX1 hash observed in
  this pass was
  `3747b8df1546e229eb0f0ec61042bdd1f64cc87c3f20f96634a2f4f4d845add0`.
- Targeted Windows validation passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_cpu_z80_test mnemos_manifests_msx_test mnemos_manifests_msx2_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_chips_cpu_z80_test|mnemos_manifests_msx_test|mnemos_manifests_msx2_test|mnemos_msx_boot_test" --output-on-failure'
```

- Data-gated Bean smoke after the fix used explicit C-BIOS paths from
  `D:\emu\msx\bios` and ROMs from `D:\emu\msx\MSX files [ROM]`. Current sorted
  top-level valid-ROM index for `bean.rom` is `SkipRoms 196`; `SkipRoms 192`
  now lands on `BATTLECH` in this corpus and should not be treated as Bean
  proof. The rerun produced `3/4` expected results with summary
  `build\scratch\msx-boot\20260628-014734-948-23188\summary.json`:
  - MSX firmware passed with hash
    `697fe93903980d26d6ef37fc76b0511c8a76656ebdc70f7be616c4dd7bac836a`.
  - MSX + logo + `bean.rom` passed with the updated hash
    `690fe4e86d89606085c0296f68d7a2fb0ab7e1ba2adfdd8df23a2f5e45cd2f9a`.
  - MSX2 C-BIOS firmware passed with hash
    `8572e9f4e74913c1a5d3de86da650c0a46ab5ec0643b2d6e5f80bf0c3fd5e1bb`.
  - MSX2 C-BIOS + sub-ROM + logo + `bean.rom` remains the tracked failure with
    hash `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573`
    and reason `PC=$CA3E after the $BFFF->$C000 handoff`.
- Specialist follow-up still points away from mapper/page-3 mirroring fixes.
  The next useful product-code investigation is a differential MSX2/MSX1 trace
  around `$8303-$8308`, `$8319/$832D`, `$0038`, and `$823D-$832D`, including
  VDP status reads and writes to `$E130-$E138`, `$E3C5`, and `$C000-$C03F`.
  Candidate surfaces remain narrow Z80 interrupt scheduling, V9938 status/IRQ
  timing, MSX2 IRQ propagation, or scheduler ordering; do not mask this by
  changing plain-ROM page-3 behavior.

2026-06-28 01:37 America/Chicago:

- Fixed a narrow MSX2 slot-overlap bug from the specialist review. When a
  machine profile maps the MSX2 sub-ROM and mapper RAM to the same expanded
  slot/subslot, the sub-ROM now occupies only its actual 16 KiB page:
  reads above the sub-ROM page can fall through to RAM, and writes to the
  sub-ROM page are ignored like ROM writes. This avoids the old asymmetry where
  page-0 sub-ROM reads were visible but writes could mutate hidden RAM behind
  the ROM, while higher pages in the same slot read as `$FF`.
- Added a regression in `src/manifests/msx2/tests/msx2_system_test.cpp` proving
  the overlapped sub-ROM page remains write-protected and page-3 mapper RAM
  remains writable/readable in the same slot profile.
- Targeted Windows validation passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test|mnemos_msx_boot_test" --output-on-failure'
```

- Data-gated smoke after the fix:
  - MSX2 C-BIOS + sub-ROM + logo + `bean.rom` remains the expected tracked
    failure with hash
    `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573` and
    reason `PC=$CA3E after the $BFFF->$C000 handoff`; summary
    `build\scratch\msx-boot\20260628-013645-283-20416\summary.json`.
  - MSX2 C-BIOS + sub-ROM + logo + `MSX2PMUS (13).rom` still passes with hash
    `ad42f52759050d8187b1cf85b0f7eefad6c9a4ede24453f92d77d9755133a986`;
    summary `build\scratch\msx-boot\20260628-013645-247-46008\summary.json`.
- Note: one build attempt hit `LNK1168` because `mnemos_msx_boot_test.exe` was
  being rebuilt while a parallel smoke run still held it. The serialized rerun
  above passed.

2026-06-28 01:25 America/Chicago:

- Ran two specialist read-only review passes over shared MSX/MSX2 and MSX2
  V9938/C-BIOS surfaces. Both reviews agreed that the active `bean.rom`
  evidence does not justify a product-code slot, mapper, VDP, or IRQ patch:
  the current C-BIOS MSX2 slot state is internally consistent, `$BFFF` is
  cartridge, `$C000` is page-3 RAM, and the 16 KiB cartridge must not be
  mirrored into page 3.
- Hardened the proof harness instead of masking the blocker:
  `scripts/msx/run-boot-smoke.ps1` now records configured golden hashes when a
  successful Catch2 run is intentionally quiet, and it classifies the known
  MSX2 Bean bad framebuffer hashes with explicit reasons.
- Added the same known-invalid framebuffer hash guard to
  `tests/golden/msx_boot_test.cpp`, so direct `mnemos_msx_boot_test` runs reject
  known C-BIOS diagnostic/startup screens and the tracked MSX2 Bean bad hashes
  even when a future bad frame is nonuniform.
- Added local ROM profiles for `bean.rom` in
  `tests/golden/msx_rom_profiles.json`: MSX1 is locked as a 600-frame C-BIOS
  pass with hash
  `8ac76412f61dbbd32e99439bf7cd43a0cdcbc9efb3328828dcefd261909a33a2`; MSX2
  remains a tracked parity blocker with the same ROM SHA-256
  `7e193f203d6b327689ec4b65681a7b1a868756d942c2cc03f381878e12d8edb8`.
- Focused data-gated validation after the harness change:
  - MSX1 C-BIOS + logo + `bean.rom` passed through
    `scripts/msx/run-boot-smoke.ps1` with summary
    `build\scratch\msx-boot\20260628-012436-641-2464\summary.json`.
  - MSX2 C-BIOS + sub-ROM + logo + `bean.rom` still failed, now with the
    explicit reason `framebuffer matched the known C-BIOS MSX2 Bean bad state at
    PC=$CA3E after the $BFFF->$C000 handoff`, summary
    `build\scratch\msx-boot\20260628-012436-650-40416\summary.json`.
  - The only local filename-marked MSX2 ROM found in the corpus,
    `MSX2PMUS (13).rom`, passed a 600-frame C-BIOS MSX2 smoke with hash
    `ad42f52759050d8187b1cf85b0f7eefad6c9a4ede24453f92d77d9755133a986`.
- `openmsx` was not found on `PATH`, so the external-control-emulator proof
  recommended by the reviewers has not been run. If installed later, use the
  exact C-BIOS MSX2 XML/profile and `bean.rom` before declaring this a confirmed
  C-BIOS/game compatibility edge rather than a Mnemos divergence.

2026-06-28 proceed pass America/Chicago:

- Used two specialist MSX/MSX2 review passes to audit the live blocker before
  editing. The shared Z80/IRQ and VDP wiring did not show a high-confidence
  product-code defect, so this checkpoint adds targeted regression coverage
  instead of a speculative hardware-behavior patch.
- Added a Z80 regression for an IRQ line already asserted before `EI`. The CPU
  now has a pinned test proving `EI` itself and the following instruction both
  run before the held maskable interrupt vectors to `$0038` in IM 1.
- Added the missing MSX-side manifest regression proving a TMS9918A frame IRQ
  wakes a HALTed Z80 through the system wiring. The synced tree already had the
  equivalent MSX2/V9938 HALT wake coverage.
- Confirmed the MSX2 `bean.rom` failure is not a blank-player invocation. The
  real firmware tests were run with explicit C-BIOS and ROM env vars from
  `D:\emu\msx\bios` and `D:\emu\msx\MSX files [ROM]\bean.rom`.
- New diagnostic split:
  - MSX1 with `cbios_main_msx1.rom` plus `cbios_logo_msx1.rom` remains alive at
    600 frames: `PC=$862E`, `halted=false`, nonuniform framebuffer hash
    `8ac76412f61dbbd32e99439bf7cd43a0cdcbc9efb3328828dcefd261909a33a2`.
  - MSX2 with `cbios_main_msx2.rom`, `cbios_sub.rom`, and
    `cbios_logo_msx2.rom` still halts at `PC=$CA3E`, `halted=true`,
    `primary=$D0`, `secondary3=$A0`, Graphics II tables blank, framebuffer hash
    `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573`.
  - MSX2 with the logo ROM omitted changes failure mode instead of fixing the
    game: final `PC=$0038`, `primary=$E1`, IRQ asserted, framebuffer hash
    `e1ba45afdb7e4df41612c4fa2fd7f354552cb33879ff9657ba45f59ffc5e0940`.
- The traced MSX2 code writing `$C000` matches `cbios_logo_msx2.rom` offset
  `$0097` (`LD DE,$C000; LD HL,$8176; LDIR`). This explains why C-BIOS logo
  state affects the later `$BFFF->$C000` Bean path, but it is not enough by
  itself to justify disabling or mutating the logo ROM mapping: the local
  `C-BIOS_MSX2.xml` intentionally maps the logo ROM in primary slot 0 at
  `$8000-$BFFF`.
- Current next investigation target: prove whether the MSX2 failure is a real
  C-BIOS compatibility edge, an interrupt/slot-state divergence after Bean
  reaches `$99DB`, or incorrect RAM mapper/slot handoff state after the logo
  program. Do not "fix" by mirroring the 16 KiB cartridge into page 3; tests now
  guard `$BFFF` as cartridge and `$C000` as RAM.
- Targeted Windows validation passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_cpu_z80_test mnemos_manifests_msx_test mnemos_manifests_msx2_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_chips_cpu_z80_test|mnemos_manifests_msx_test|mnemos_manifests_msx2_test|mnemos_msx_boot_test" --output-on-failure'
```

New logs:

```text
build\scratch\msx-bean-diagnostics\trace-20260628-proceed3\msx1-bean-logo-600.log
build\scratch\msx-bean-diagnostics\trace-20260628-proceed3\msx2-bean-logo-600.log
build\scratch\msx-bean-diagnostics\trace-20260628-proceed3\msx2-bean-no-logo-600.log
```

2026-06-28 follow-up America/Chicago:

- Specialist audit pass completed for shared MSX/MSX2 surfaces. The Z80
  `RETN/RETI` path already restored `IFF1` from `IFF2`; a regression now pins
  both opcodes so the failing MSX2 interrupt-disabled end state is not chased as
  an untested CPU-return gap. The MSX2 slot audit confirmed the active
  `bean.rom` profile is a valid `expanded=8`, sub-ROM `3.0`, RAM `3.2`, 512K
  mapper setup, and that the `$BFFF->$C000` transition is a legitimate
  cartridge-to-RAM boundary.
- Added an exact MSX2 manifest regression for that C-BIOS-style profile:
  primary slot `$D0`, secondary slot 3 `$A0`, page 2 as a 16 KiB plain
  cartridge, and page 3 as mapper RAM segment 0. This deliberately prevents
  accidental "fixes" that overlap sub-ROM/RAM or mirror a 16 KiB cart into page
  3.
- Extended VDP I/O diagnostics to include data-port `$98` writes and the CPU
  VRAM address latch for both TMS9918A and V9938. The MSX2 trace shows real
  cartridge-time VDP writes to addresses such as `$1670`, `$12B0`, and `$02C0`,
  while the active Graphics II tables (`NT=$1800`, `PT=$0000`, `CT=$2000`)
  remain backdrop-only at the final failing frame.
- New diagnostics:

```text
build\scratch\msx-bean-diagnostics\vdp-data-20260628\
build\scratch\msx-bean-diagnostics\vdp-read-20260628\msx2-vdp-read.log
build\scratch\msx-bean-diagnostics\post-regression-20260628\msx1-bean-600.log
build\scratch\msx-bean-diagnostics\post-regression-20260628\msx2-bean-slotprofile-600.log
```

- Targeted Windows validation passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_cpu_z80_test mnemos_manifests_msx2_test mnemos_chips_video_v9938_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_chips_cpu_z80_test|mnemos_manifests_msx2_test|mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

- Explicit C-BIOS + `bean.rom` validation was rerun with proper system/ROM
  inputs. MSX1 exits cleanly after 600 frames. MSX2 still fails with
  `PC=$CA3E`, `halted=true`, `iff1=false`, `iff2=false`, `primary=$D0`,
  `secondary3=$A0`, RAM mapper segments `[3,2,1,0]`,
  `mode=graphics_ii(4)`, and framebuffer hash
  `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573`.
  This remains not a 100% operational state.

2026-06-28 America/Chicago:

- Added named VDP mode diagnostics in `tests/golden/msx_boot_test.cpp`:
  `mode=graphics_ii(4)` instead of raw `mode=4`, and equivalent names for
  TMS9918A modes.
- Extended the MSX/MSX2 PC and VDP I/O watches with `iff1`, `iff2`, interrupt
  mode, halted state, VDP IRQ state, selected VDP status, and named VDP mode.
- Relabeled the SCREEN 5/Graphic 4 byte scan as inactive raw page data when
  V9938 is not actually in Graphic 4. The failing `bean.rom` profile now says
  `screen5_page_active=false`, while active Graphics II samples/histograms stay
  all backdrop.
- Added a V9938 regression proving enum value `4` is Graphics II, while the
  existing SCREEN 5 test continues to cover real Graphic 4 rendering.
- Targeted Windows validation passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_chips_video_v9938_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

- Explicit C-BIOS + `bean.rom` slot-profile validation still reproduces the
  blocker. With `expanded=8`, sub-ROM `3.0`, RAM `3.2`, RAM size `512K`, MSX2
  still ends at `PC=$CA3E`, `halted=true`, `mode=graphics_ii(4)`, `irq=true`,
  `screen5_page_active=false`, framebuffer hash
  `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573`.
  This is not a 100% operational state.
- New log:

```text
build\scratch\msx-bean-diagnostics\mode-labels-20260628\msx2-bean-slotprofile-600-mode-labels.log
```

2026-06-27 late evening America/Chicago:

- Fixed shared MSX/MSX2 PPI port-A behavior: `$A8` writes now latch `ppi_a`
  and only update the primary slot output when PPI port A is configured as an
  output. This affects `src/manifests/msx/msx_system.cpp` and
  `src/manifests/msx2/msx2_system.cpp`.
- Corrected V9938 S#0 read semantics: reading S#0 clears frame IRQ (`F`) and
  sprite collision (`C`) while preserving fifth-sprite overflow (`5S`) and the
  low sprite index bits. This affects `src/chips/video/v9938/v9938.cpp`.
- Added MSX and MSX2 regressions for PPI `$A8` direction plus 16 KiB
  upper-page cartridge `$BFFF->$C000` boundary behavior. The boundary tests
  deliberately prove that `$BFFF` is cartridge and `$C000` remains page-3 RAM
  when the slot register selects page 2 as cartridge and page 3 as RAM.
- Targeted Windows validation passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx_test mnemos_manifests_msx2_test mnemos_chips_video_v9938_test mnemos_msx_boot_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx_test|mnemos_manifests_msx2_test|mnemos_chips_video_v9938_test|mnemos_msx_boot_test" --output-on-failure'
```

- Explicit C-BIOS + `bean.rom` validation is still not green. MSX2 still halts
  at `PC=$CA3E`, `halted=true`, with framebuffer hash
  `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573`.
  This is not a 100% operational state.
- MSX1 with the same `bean.rom` remains alive after the PPI fix:
  `PC=$862E`, `halted=false`, nonuniform framebuffer, forced diagnostic hash
  `8ac76412f61dbbd32e99439bf7cd43a0cdcbc9efb3328828dcefd261909a33a2`.
- New logs:

```text
build\scratch\msx-bean-diagnostics\post-ppi-s0-fix-20260627\msx2-bean-after-ppi-s0.log
build\scratch\msx-bean-diagnostics\post-ppi-s0-fix-20260627\msx2-bean-900frames-after-ppi-s0.log
build\scratch\msx-bean-diagnostics\post-ppi-s0-fix-20260627\msx1-bean-after-ppi-s0.log
```

Open issue for the next pass:

- Do not treat the raw SCREEN 5 bytes as active Graphic 4 output. The failing
  profile is active Graphics II (`mode=graphics_ii(4)`) and the Graphics II
  visible tables remain backdrop-only. Continue from the `$8303-$832D`/
  `$99DB` control-flow evidence and interrupt/IRQ timing around `EI`, not the
  Graphic 4 renderer.
- The `$BFFF->$C000` transition is now protected by tests as a valid cart/RAM
  boundary, not something to "fix" by mirroring the cartridge into page 3.

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

The root checkout at `C:\dev\emu\Mnemos` is not the active MSX/MSX2 worktree.

Repository rules that matter most:

- Read `CONSTITUTION.md` and `README.md` before toolchain work.
- Product code lives under `src/`; build, log, and diagnostic output belongs
  under `build/`.
- Do not commit ROMs, firmware, screenshots, logs, build outputs, or generated
  scratch artifacts.
- Other emulators are L5 reference material only. Do not lift emulator source.
- A blank Mnemos Player window is not proof. Player proof needs explicit
  `--system` and `--rom` arguments.

## User Contract

- Implement both MSX and MSX2. They share common manifests, cartridge mapper
  code, player adapters, VDP behavior, smoke scripts, and golden-test surfaces.
- Preserve the requested branch and worktree: `feature/msx2` at
  `C:\dev\emu\Mnemos-msx2`.
- C-BIOS root: `D:\emu\msx\bios`.
- ROM corpus root: `D:\emu\msx\MSX files [ROM]`.
- Use `Test-Path -LiteralPath` for ROM corpus paths because `[ROM]` is
  wildcard syntax in PowerShell.
- Do not claim "100% working" until real ROM/media validation proves it.

## Current Verdict

MSX/MSX2 are partially working, not complete.

Confirmed:

- C-BIOS MSX and MSX2 boot paths are wired through golden tests and explicit
  player launch paths.
- Shared RAM-size profile semantics, cartridge mapper resolution, smoke-script
  routing, and MSX/MSX2 golden-test hooks are in place.
- Earlier bounded real-ROM smoke windows passed through skip 191.
- Focused tests passed at the last validation checkpoint:
  `mnemos_msx_boot_test`, `mnemos_chips_video_v9938_test`, and
  `mnemos_manifests_msx2_test`.
- The worktree was clean and aligned with `origin/feature/msx2` before this
  handoff refresh.

Active blocker:

- Skip-192 still fails on MSX2 for `bean.rom`.
- MSX `bean.rom` stays alive and renders a nonuniform framebuffer in the same
  scenario.
- MSX2 `bean.rom` halts at `$CA3E` after divergent control flow around
  `$832D`, `$99DB`, `$BFFF`, and `$C000`.
- The goal remains open until this blocker and broader real-ROM coverage pass.

## Current Commits

The latest commits before this handoff refresh are:

```text
33be6e40 Add MSX diagnostic watch cycle filters
3b1ba0e5 Refresh MSX2 resume handoff
1d43f8f0 Add MSX boot slot-state PC diagnostics
01c489c9 Narrow MSX2 bean ROM diagnostics
e2fbcd07 Add MSX2 bean ROM trace diagnostics
2bfd161e Refresh MSX2 resume handoff
```

`33be6e40` adds min-cycle filters to the diagnostic watchers and records the
fresh post-handoff trace below.

## Test And Build Entry Points

Windows validation should run under Visual Studio DevCmd:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_manifests_msx2_test mnemos_msx_boot_test mnemos_chips_video_v9938_test && ctest --preset windows-msvc-debug -R "mnemos_manifests_msx2_test|mnemos_msx_boot_test|mnemos_chips_video_v9938_test" --output-on-failure'
```

For targeted forced diagnostics, run the golden test executable directly after
setting the environment variables in the relevant section below. Exit code `42`
can be intentional when `MNEMOS_MSX2_BOOT_SHA256=force-diagnostics` is used.

## BIOS And ROM Inputs

C-BIOS files verified during this session:

```text
D:\emu\msx\bios\cbios\cbios_main_msx1.rom
D:\emu\msx\bios\cbios\cbios_logo_msx1.rom
D:\emu\msx\bios\cbios\cbios_main_msx2.rom
D:\emu\msx\bios\cbios\cbios_sub.rom
D:\emu\msx\bios\cbios\cbios_logo_msx2.rom
```

The active blocker ROM:

```text
path: D:\emu\msx\MSX files [ROM]\bean.rom
size: 16384
sha256: 7e193f203d6b327689ec4b65681a7b1a868756d942c2cc03f381878e12d8edb8
```

## Diagnostic Surfaces Added

`tests/golden/msx_boot_test.cpp` includes gated diagnostics useful for the
current blocker:

- `MNEMOS_MSX_PC_WATCH` traces PC transitions and now includes fixed work bytes
  used by the `bean.rom` dispatcher: `$E12D`, `$E132`, `$E168`, `$E3C5`,
  `$D800`, `$D801`, `$F3DF`, and `$FCAF`.
- PC-watch output also includes `ix_window=...` and `sp_window=...` windows.
- `MNEMOS_MSX_VDP_IO_WATCH_KIND=read|write|all` filters VDP I/O tracing.
- `MNEMOS_MSX_MEM_WATCH` captures memory accesses for selected ranges.
- New post-handoff filters suppress firmware warmup so real-ROM diagnostics can
  reach the cartridge window without exhausting event caps:
  `MNEMOS_MSX_PC_WATCH_MIN_CYCLES`,
  `MNEMOS_MSX_MEM_WATCH_MIN_CYCLES`,
  `MNEMOS_MSX_VDP_IO_WATCH_MIN_CYCLES`, and shared fallback
  `MNEMOS_MSX_WATCH_MIN_CYCLES`.

Scratch logs from the current investigation were intentionally left under:

```text
build\scratch\msx-bean-diagnostics\
build\scratch\msx-bean-diagnostics\focused-20260627\
build\scratch\msx-bean-diagnostics\matrix-20260627\
build\scratch\msx-bean-diagnostics\slot-factor-20260627\
build\scratch\msx-bean-diagnostics\enriched-pcwatch-20260627\
build\scratch\msx-bean-diagnostics\resume-20260627\
```

These logs are not committed.

## Strongest Current Evidence

MSX2 `bean.rom` reads V9938 S#0 at `$8303/$8305` and stores the result in
`$E3C5`.

The four cartridge-PC VDP reads in the failing MSX2 run were:

```text
cycle 14093184: value $C4, S#0 after read $44, mode graphics_i,  frame 235
cycle 14562955: value $C4, S#0 after read $44, mode graphics_ii, frame 243
cycle 14563297: value $44, S#0 after read $44, mode graphics_ii, frame 243
cycle 14564741: value $44, S#0 after read $44, mode graphics_ii, frame 243
```

The exact `$832D` watch shows `$E3C5` changes from `$C4` to `$44` before the
third and fourth dispatcher jumps. The fourth jump is:

```text
previous_pc=$832D current_pc=$99DB cycles=14564897
work=e12d:$00 e132:$00 e168:$00 e3c5:$44 d800:$00 d801:$00 f3df:$00 fcaf:$01
```

MSX1 with the same cartridge-PC read-only VDP trace produced no VDP read events
before the 600-frame forced hash dump and remains alive at `$829D`.

Cartridge-filtered writes to `$C000-$C03F` show `bean.rom` writes data-like
bytes there. It does not install executable code before the `$BFFF` call.

A slot-enriched `$BFFF-$C020` trace confirms the failing transition occurs with:

```text
slots=[0.0,0.0,1.0,3.2]
primary=$D0
secondary3=$A0
ram_segments=[3,2,1,0]
```

Page 2 is the primary cartridge, while page 3 is RAM segment 0. There is no late
slot-select race at the `$99E3->$BFFF` and `$BFFF->$C000` boundary.

## Hot Path Disassembly Notes

No local Z80 disassembler was found in PATH. The following notes are manual
decode of the hot bytes in `bean.rom`.

Dispatcher around `$82F9`:

```text
$82F9: LD IX,$832E
$82FC: LD A,($E12D)
$82FF: OR A
$8300: CALL NZ,$823D
$8303: IN A,($99)
$8305: LD ($E3C5),A
$8308: EI
$8309: LD A,(IX+1)
$830C: OR A
$830D: RET Z
$830E: PUSH IX
$8310: PUSH DE
$8311: ADD IX,DE
$8313: LD L,(IX+0)
$8316: LD H,(IX+1)
$8319: CALL $832D
$832D: JP HL
```

The fourth `$832D` hit selects `$99DB` through a ROM table, not RAM corruption:

```text
range previous_pc=$8319 current_pc=$832D cycles=14564893
af=$A68C bc=$FFA1 de=$0002 hl=$99DB ix=$99CF sp=$E3B1 ret0=$831C
ix_window=[$99CF=$DB,$99D0=$99,$99D1=$45,$99D2=$9C,$99D3=$46,$99D4=$9C,$99D5=$56,$99D6=$9C,$99D7=$57,$99D8=$9C,$99D9=$3C,$99DA=$A1,$99DB=$21,$99DC=$FD,$99DD=$99,$99DE=$11]
sp_window=[$E3B1=$831C,$E3B3=$0002,$E3B5=$99CD,$E3B7=$8226]
```

`$99DB` begins with plausible code:

```text
LD HL,$99FD
LD DE,$0040
LD B,$06
CALL $BFFF
LD HL,$9A2D
LD DE,$0058
LD B,$04
CALL $BFDF
LD HL,$9A4D
LD DE,$0060
LD B,$10
CALL $BFFF
RET
```

The bad-looking fallthrough:

```text
range previous_pc=$99E3 current_pc=$BFFF cycles=14564941
af=$A68C bc=$06A1 de=$0040 hl=$99FD ix=$99CF sp=$E3AF ret0=$99E6
sp_window=[$E3AF=$99E6,$E3B1=$831C,$E3B3=$0002,$E3B5=$99CD]

range previous_pc=$BFFF current_pc=$C000 cycles=14564952
af=$A68C bc=$06A1 de=$0040 hl=$99FD ix=$99CF sp=$E3AD ret0=$06A1
sp_window=[$E3AD=$06A1,$E3AF=$99E6,$E3B1=$831C,$E3B3=$0002]
code=[$BFF8=$08,$BFF9=$00,$BFFA=$09,$BFFB=$C1,$BFFC=$10,$BFFD=$E1,$BFFE=$C9,$BFFF=$C5,$C000=$00,$C001=$00,$C002=$27,$C003=$03,$C004=$17,$C005=$01,$C006=$00,$C007=$00]
```

Interpretation:

- `CALL $BFFF` pushes return `$99E6`.
- `$BFFF=$C5` (`PUSH BC`) pushes `$06A1`, so top-of-stack becomes `$06A1`.
- PC enters `$C000`, where page-3 bytes are data-like RAM.
- Execution continues through data-like bytes and eventually halts at `$CA3E`.
- The wrong decision likely happens earlier around `$832D/$99DB`.

## Important Corrections And Non-Fixes

- Do not "fix" this by clearing V9938 S#0 bit 6 on status read. The V9938
  manual says S#0 read resets the F flag. Existing V9938 tests intentionally
  preserve sprite overflow/collision state after an S#0 read.
- A temporary experiment that changed V9938 S#0 read to `status_[0] &= 0x1F`
  broke existing V9938 tests and did not fix `bean.rom`; MSX2 still halted at
  `$CA3E` with framebuffer hash
  `9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573`.
- Do not blindly map 16 KiB plain ROM into page 3. Existing tests intentionally
  cover non-32 KiB plain cartridges remaining unmapped outside their valid
  window.
- An earlier handoff interpreted `mode=4` as V9938 Graphics IV. That was wrong.
  In `src/chips/video/v9938/v9938.hpp`, enum value `4` is Graphics II.
  Failing `bean.rom` ends in TMS-compatible Graphics II, not V9938 bitmap mode.

## Slot/Profile Matrix Result

A C-BIOS/slot matrix was already run for `bean.rom`. No BIOS/slot combination
is a clean production profile.

Result summary:

- `cbios_main_msx2.rom`, `_eu`, `_br`, and `_jp` all behave the same for the
  meaningful layouts.
- Leaving slot overrides unset avoids the `$CA3E` halt but is not a fix:
  `primary=$30`, VDP registers remain zero, `vram_nonzero=0`, and the
  framebuffer is uniform blank.
- `sub_slot=3.0`, `ram_slot=3.2` reproduces the active failure:
  `PC=$CA3E`, halted, `primary=$D0`, `secondary3=$A0`, Graphics II mode.
- `sub_slot=3.1`, `ram_slot=3.0` and `sub_slot=3.1`, `ram_slot=3.2` also halt
  at `$CA3E`.
- `sub_slot=3.0` with RAM left at default `3.0` renders a nonuniform frame but
  is invalid proof: reads come from sub-ROM before RAM in `read_slot()`, while
  writes still go to RAM, so sub-ROM and RAM overlap at `3.0`.

The matrix rules out a simple profile-only fix.

## Post-Handoff Trace Update

The current diagnostic harness was rebuilt and rerun with:

```text
MNEMOS_MSX_PC_WATCH=$832D-$832D
MNEMOS_MSX_PC_WATCH_MIN_CYCLES=13000000
```

The four cartridge-time `$832D` dispatches are now captured without early
firmware noise:

```text
cycles=14093336 hl=$833C ix=$8330 work=e12d:$00 e132:$00 e168:$00 e3c5:$C4 fcaf:$01
cycles=14563107 hl=$8D8A ix=$8D7E work=e12d:$00 e132:$00 e168:$00 e3c5:$C4 fcaf:$01
cycles=14563449 hl=$9471 ix=$9465 work=e12d:$00 e132:$00 e168:$00 e3c5:$44 fcaf:$01
cycles=14564893 hl=$99DB ix=$99CF work=e12d:$00 e132:$00 e168:$00 e3c5:$44 fcaf:$01
```

Additional ROM search found `bean.rom` writes `$E3C5` at `$8305` but has no
`LD A,($E3C5)` or other direct read of that address in the ROM image. The VDP
status values are useful timing evidence, but this specific bad path is not a
direct status-byte branch.

The `$9471` script resets several work pointers and jumps to `$96BB`; after it
returns, the dispatcher promotes `IX` from the first word of the `$9463` table:
`[$9463]=$99CD`. The next loop adds `DE=2`, reads `$99CF`, and dispatches
`$99DB`.

A late memory watch with:

```text
MNEMOS_MSX_MEM_WATCH=$C000-$C03F
MNEMOS_MSX_MEM_WATCH_MIN_CYCLES=13000000
```

captured no writes. The `$C000` page-3 RAM bytes are prepared before cycle 13M
and are static by the bad `$BFFF` call. The final halt remains:

```text
cpu pc/sp/af/bc/de/hl: $CA3E/$E6FF/$5FBA/$0101/$5F01/$66B8 halted=true
boot framebuffer sha256: 9886081a3b6b33ef4cf5e20210f70b398d8b8782f37e33ab69f858cf2cd39573
```

## Repro Environment

MSX2 forced diagnostic base:

```powershell
$env:MNEMOS_MSX2_BIOS='D:\emu\msx\bios\cbios\cbios_main_msx2.rom'
$env:MNEMOS_MSX2_SUB_ROM='D:\emu\msx\bios\cbios\cbios_sub.rom'
$env:MNEMOS_MSX2_LOGO_ROM='D:\emu\msx\bios\cbios\cbios_logo_msx2.rom'
$env:MNEMOS_MSX2_ROM='D:\emu\msx\MSX files [ROM]\bean.rom'
$env:MNEMOS_MSX2_EXPANDED_SLOTS='8'
$env:MNEMOS_MSX2_SUB_SLOT='3.0'
$env:MNEMOS_MSX2_RAM_SLOT='3.2'
$env:MNEMOS_MSX2_RAM_SIZE='512K'
$env:MNEMOS_MSX2_REGION='ntsc'
$env:MNEMOS_MSX2_BOOT_FRAMES='600'
$env:MNEMOS_MSX2_BOOT_SHA256='force-diagnostics'
```

Focused traces:

```powershell
$env:MNEMOS_MSX_PC_WATCH='$832D-$832D'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe 'msx2 boots real firmware to a deterministic golden framebuffer'
```

```powershell
$env:MNEMOS_MSX_PC_WATCH='$BFFF-$C020'
$env:MNEMOS_MSX_MEM_WATCH='$C000-$C03F'
.\build\windows-msvc-debug\tests\golden\mnemos_msx_boot_test.exe 'msx2 boots real firmware to a deterministic golden framebuffer'
```

Use a single range for `MNEMOS_MSX_PC_WATCH`; comma-separated watch values are
not parsed as multiple ranges.

## Recommended Next Work

Do not start with broad mapper changes or a ROM profile. The highest-value
root-cause slice is why the MSX2 game script selects the `$99CF -> $99DB` table
path and then reaches a one-byte `$BFFF` helper whose fallthrough lands in
page-3 RAM data.

Recommended probes:

1. Inspect Z80 EI/IRQ timing before changing mapper or VDP behavior. The bad
   path crosses `EI` at `$8308`, and the next useful source files are:
   `src/chips/cpu/z80/z80.cpp`, `src/chips/cpu/z80/z80.hpp`,
   `src/chips/cpu/z80/tests/z80_test.cpp`, and MSX2 IRQ propagation in
   `src/manifests/msx2/msx2_system.cpp`.
2. Run a late-cycle PC watch on `$0038-$0038` and `$8308-$8319` to confirm
   whether an interrupt is accepted between the dispatcher status read and the
   fourth `$832D` jump.
3. Continue disassembling `$9471->$96BB` and the `$99CD/$99DB` table path to
   determine whether the `$BFFF` fallthrough is intentional generated-RAM code,
   a page/slot expectation, or a prior-state corruption symptom.
4. Trace writes to `$C000-$C03F` without the min-cycle filter only as needed,
   then correlate the writer PC with the `$99DB` data tables. Existing late
   memory-watch evidence already shows no writes after cycle 13M.
5. If a code fix emerges, run the focused Windows validation command above,
   then rerun bounded MSX/MSX2 real-ROM smoke through skip 192 and beyond.

## Smoke Runner Reminder

Inspect `scripts/msx/run-boot-smoke.ps1` before using it. A likely invocation
shape is:

```powershell
.\scripts\msx\run-boot-smoke.ps1 `
  -BuildDir build/windows-msvc-debug `
  -Msx2Bios 'D:\emu\msx\bios\cbios\cbios_main_msx2.rom' `
  -Msx2SubRom 'D:\emu\msx\bios\cbios\cbios_sub.rom' `
  -Msx2LogoRom 'D:\emu\msx\bios\cbios\cbios_logo_msx2.rom' `
  -Msx2RomDir 'D:\emu\msx\MSX files [ROM]' `
  -RomProfileManifest tests/golden/msx_rom_profiles.json `
  -Frames 600 `
  -SkipRoms 191 `
  -MaxRoms 16
```

Adjust parameters based on the script's current source and help output.

## Stop Condition

Do not mark the MSX/MSX2 goal complete until:

- `bean.rom` MSX2 skip-192 is understood and either fixed or correctly
  profiled.
- Bounded smoke passes through the previous failure point and records hashes.
- MSX and MSX2 both have explicit real-ROM proof, not just unit tests.
- Any code changes are covered by focused tests plus the targeted Windows
  validation sweep.
