# Amiga500 Feature Handoff

Date: 2026-06-25

Workspace: `C:\dev\emu\Mnemos-Amiga500`

Branch: `feature/Amiga500`

Base before checkpoint commit: `713ddf37 Split large CPU/NES test files into focused suites`

Status: blocked, not complete. The branch contains a large Amiga500 bring-up
checkpoint, but the acceptance target "100% working Amiga500 emulation" is not
met. Real Kickstart 1.3 plus a compressed Cannon Fodder ADF still renders a
black screenshot, and Kickstart/trackdisk has not started disk DMA.

## Start Here

1. Open `C:\dev\emu\Mnemos-Amiga500`.
2. Confirm the branch is `feature/Amiga500`.
3. Read `CONSTITUTION.md`, `README.md`, then this file.
4. Pull the checkpoint branch with `git pull --ff-only origin feature/Amiga500`.
5. Use the Windows validation path from `README.md`; do not treat green unit
   tests as completion until real Kickstart/ADF rendering works.

## What Landed

- Player Amiga500 launch path: `--system amiga500` loads Kickstart from
  `MNEMOS_AMIGA500_KICKSTART`, treats the user `--rom` as disk media, and can
  detect/mount ADFs inside ZIP files.
- Amiga500 adapter: media capability reporting, resident Kickstart detection,
  compressed ADF disk mount, controller/keyboard handling, deterministic
  adapter state, crash/debug artifacts, and trace instrumentation.
- M68000: privileged `RESET` now pulses an external reset callback without
  reloading CPU vectors, plus focused fixes/tests for user-mode `RTE`, full
  Exec-style `MOVEM` restore, and `TST.B d16(An)` condition codes.
- CIA 6526 / Amiga 8520: pin-level `port_b_pins()`, one-shot high-byte timer
  semantics split between 6526 and 8520, and Amiga timer interrupt coverage.
- Amiga500 board reset: CPU `RESET` is wired to a full board reset hook that
  reasserts Kickstart overlay, clears custom state/interrupts/DMA, preserves
  mounted disks, and deselects/motor-offs floppy drives.
- OCS/custom path: copper pointer clipping, custom byte-write lane behavior,
  interrupt visibility, disk DMA pointer/length protection from copper, disk
  stream phasing, weak-bit behavior, and additional save-state coverage.
- Agnus/video path: more bitplane, sprite, copper, blitter, and bus-contention
  timing behavior with focused regression coverage.
- Floppy status model: external drives now have an explicit `connected` state.
  DF0 is always connected; DF1-DF3 are disconnected until mounted. CIA-A disk
  sense pins report high for absent selected external drives, matching the
  behaviour checked against WinUAE/vAmiga as L5 guidance.
- Save-state schema: Amiga500 system state is now version `23` and serializes
  floppy drive connection state.
- Diagnostics: `MNEMOS_AMIGA500_CPU_TRACE`, `MNEMOS_AMIGA500_RAM_TRACE`,
  `MNEMOS_AMIGA500_RAM_TRACE_READS`, `MNEMOS_AMIGA500_CIA_TRACE`, and disk
  trace output were expanded. CPU/RAM trace includes A4/A5; CIA trace logs
  active-low pin fields and active drive connection state.

## Important Local Media

These paths were used during live validation and are intentionally not
committed:

- Kickstart: `D:\emu\amiga\bios\Kickstart 1.3.rom`
- Compressed ADF: `D:\emu\amiga\adf\Cannon Fodder (1993)(Virgin)(DE)(Disk 1 of 3)[cr TRSI].zip`
- ADF folder: `D:\emu\amiga\adf`

## Validation Already Run

Latest focused validation on 2026-06-25 passed: 7/7 tests covering M68000,
CIA 6526, CIA 8520, Agnus, Amiga500 manifest, Amiga500 adapter, compressed
system launch, and `mnemos_player`.

Command:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --parallel 1 --target mnemos_chips_cpu_m68000_test mnemos_chips_bus_controller_cia_6526_test mnemos_chips_peripheral_cia8520_test mnemos_chips_video_agnus_test mnemos_manifests_amiga500_test mnemos_apps_player_amiga500_adapter_test mnemos_apps_player_system_launch_test mnemos_player && ctest --preset windows-msvc-debug --output-on-failure -R "mnemos_chips_cpu_m68000_test|mnemos_chips_bus_controller_cia_6526_test|mnemos_chips_peripheral_cia8520_test|mnemos_chips_video_agnus_test|mnemos_manifests_amiga500_test|mnemos_apps_player_system_launch_test|mnemos_apps_player_amiga500_adapter_test"'
```

Earlier focused M68000 build/test passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --parallel 1 --target mnemos_chips_cpu_m68000_test mnemos_player'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug --output-on-failure -R mnemos_chips_cpu_m68000_test'
```

Focused Amiga500 manifest/adapter build/test passed:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --parallel 1 --target mnemos_manifests_amiga500_test mnemos_apps_player_amiga500_adapter_test mnemos_player && ctest --preset windows-msvc-debug --output-on-failure -R "mnemos_manifests_amiga500_test|mnemos_apps_player_amiga500_adapter_test"'
```

Real Kickstart/ADF smoke still fails visually:

```powershell
$env:MNEMOS_AMIGA500_KICKSTART='D:\emu\amiga\bios\Kickstart 1.3.rom'
.\build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system amiga500 --rom 'D:\emu\amiga\adf\Cannon Fodder (1993)(Virgin)(DE)(Disk 1 of 3)[cr TRSI].zip' --screenshot 'build\scratch\amiga-kickstart13-cannon-latest.png' --frames 900
```

Latest handoff smoke on 2026-06-25 also wrote a black screenshot:

```powershell
$env:MNEMOS_AMIGA500_KICKSTART='D:\emu\amiga\bios\Kickstart 1.3.rom'
$adf='D:\emu\amiga\adf\Cannon Fodder (1993)(Virgin)(DE)(Disk 1 of 3)[cr TRSI].zip'
.\build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system amiga500 --rom $adf --screenshot 'build\scratch\amiga-handoff-smoke-900.png' --frames 900
```

Artifact: `build\scratch\amiga-handoff-smoke-900.png`, 320x256, 318 bytes,
visually black.

Latest traced smoke command:

```powershell
$env:MNEMOS_AMIGA500_KICKSTART='D:\emu\amiga\bios\Kickstart 1.3.rom'
$env:MNEMOS_AMIGA500_CIA_TRACE='1'
$env:MNEMOS_AMIGA500_CPU_TRACE='FE9000-FE94E0,FEA000-FEAB00,FC4800-FC4B00'
Remove-Item Env:MNEMOS_AMIGA500_RAM_TRACE -ErrorAction SilentlyContinue
Remove-Item Env:MNEMOS_AMIGA500_RAM_TRACE_READS -ErrorAction SilentlyContinue
.\build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system amiga500 --rom 'D:\emu\amiga\adf\Cannon Fodder (1993)(Virgin)(DE)(Disk 1 of 3)[cr TRSI].zip' --screenshot 'build\scratch\amiga-drive-presence-probe-900.png' --frames 900 *> 'build\scratch\amiga-drive-presence-probe-900.log'
```

Artifacts from that run:

- `build\scratch\amiga-drive-presence-probe-900.log`
- `build\scratch\amiga-drive-presence-probe-900.png`
- `build\scratch\amiga-drive-presence-probe-900.png.*`

The screenshot is still black. Do not call this branch complete.

## Current Blocker

The most recent investigation moved the blocker forward but did not close it.

- Kickstart now gets through more of Exec/trackdisk than before.
- Timer reply/wakeup is working: timer request `006176` is dequeued, Exec
  Signal wakes task `003342` with signal `40000000`, and Wait resumes.
- External drive probes are fixed: DF1-DF3 now report disconnected/high CIA-A
  disk sense pins until mounted.
- DF0 still fails before disk DMA. Trace shows DF0 is selected and motor-on at
  Kickstart PC around `FEA112`, then the disk.resource path around `FC4A86`
  writes `BFD100 FF`, deselects all drives, and turns the motor off before any
  real `DFF020`/`DFF022`/`DFF024`/`DFF026` disk DMA setup.
- Later trackdisk polling loops around `FE5A72` with no selected drive and CIA-A
  disk sense pins high.

Likely remaining area: Kickstart/disk.resource is rejecting or backing out of
DF0 due to readiness/change/motor/unit state semantics, not because timer
reply is missing. The next agent should prove this from traces before patching.

## Best Next Trace

Run a focused RAM/CPU trace around the disk.resource/trackdisk structures:

```powershell
$env:MNEMOS_AMIGA500_KICKSTART='D:\emu\amiga\bios\Kickstart 1.3.rom'
$env:MNEMOS_AMIGA500_CIA_TRACE='1'
$env:MNEMOS_AMIGA500_CPU_TRACE='FE9000-FE94E0,FE9600-FE9700,FEA000-FEAB00,FC4A00-FC4B00'
$env:MNEMOS_AMIGA500_RAM_TRACE='001F00:0080,0043C0:0060,004700:0080,006100:0180,006490:0080'
$env:MNEMOS_AMIGA500_RAM_TRACE_READS='1'
.\build\windows-msvc-debug\src\apps\player\mnemos_player.exe --system amiga500 --rom 'D:\emu\amiga\adf\Cannon Fodder (1993)(Virgin)(DE)(Disk 1 of 3)[cr TRSI].zip' --screenshot 'build\scratch\amiga-trackdisk-struct-probe-360.png' --frames 360 *> 'build\scratch\amiga-trackdisk-struct-probe-360.log'
```

Then map the exact Kickstart routines around:

- `FEA10C`, `FEA112`, `FEA11A`, `FEA11E`, `FEA124`, `FEA190`
- `FC4A76`, `FC4A86`
- `FE5A72`, `FE9000-FE94E0`, `FE9600-FE9700`

Useful public references consulted as L5 guidance only:

- <https://pastraiser.com/computers/amiga/disk.resource_disassembly.txt>
- <http://wandel.ca/homepage/execdis/exec_disassembly.txt>
- <https://pastraiser.com/computers/amiga/strap_disassembly.txt>
- <https://wiki.amigaos.net/wiki/Trackdisk_Device>
- <https://github.com/tonioni/WinUAE/blob/master/disk.cpp>
- <https://github.com/tonioni/WinUAE/blob/master/cia.cpp>
- <https://github.com/dirkwhoffmann/vAmiga/blob/master/Core/Peripherals/Drive/FloppyDrive.cpp>

## Files Most Relevant To Resume

- `src\manifests\amiga500\amiga500_system.cpp`
- `src\manifests\amiga500\amiga500_system.hpp`
- `src\manifests\amiga500\tests\amiga500_system_test.cpp`
- `src\apps\player\adapters\amiga500\amiga500_adapter.cpp`
- `src\apps\player\adapters\amiga500\amiga500_adapter.hpp`
- `src\apps\player\adapters\amiga500\tests\amiga500_adapter_test.cpp`
- `src\apps\player\system_launch.cpp`
- `src\apps\player\tests\system_launch_test.cpp`
- `src\chips\cpu\m68000\m68000.cpp`
- `src\chips\bus_controller\cia_6526\cia_6526.cpp`
- `src\chips\peripheral\cia8520\cia8520.cpp`
- `src\chips\video\agnus\agnus.cpp`

## Completion Bar

Do not mark the goal complete until all of these are true:

- Real Kickstart boots far enough to render visible output.
- A compressed ADF from `D:\emu\amiga\adf` mounts and is actually consumed by
  trackdisk/disk DMA.
- The screenshot is non-black and backed by a saved artifact under `build\`.
- Focused regressions pass.
- The canonical Windows build/test path still passes for the touched targets.
