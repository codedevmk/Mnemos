# Amiga Compatibility Inventory

This is the living compatibility ledger for Mnemos Amiga support. It is a
tasklist, not a final compatibility claim: a cell stays `TBD` until that exact
software + model + Kickstart route has current-build evidence.

## Status Legend

| Status | Meaning |
| --- | --- |
| `TBD` | Not yet verified for this model/Kickstart route. |
| `Operational (smoke)` | Current build loads the media, runs the requested smoke window, renders a nonblack frame, and meets the configured headless speed floor. |
| `Operational (disk smoke)` | `Operational (smoke)` plus the Amiga board register dump shows disk DMA pointer programming and the output is not a known Kickstart insert-disk prompt. |
| `Operational (visual smoke)` | `Operational (disk smoke)` plus a manual screenshot spot-check showed a plausible title, loader, application, or Workbench screen for that media. |
| `Operational (A/V smoke)` | `Operational (smoke)` plus rendered-audio export produced nonzero output under the documented probe. |
| `Operational (BIOS smoke)` | Current build boots far enough with this Kickstart route to support the listed smoke-proven ADF paths. |
| `Prompt only` | Current build reaches a known Kickstart insert-disk prompt, so the title is not proven operational on that route. |
| `Requirement/error prompt` | Current build reaches title-owned or Workbench-owned UI, but that UI reports a missing requirement or error before useful execution. |
| `Visual defect` | Current build passes mechanical smoke gates but manual screenshot review shows black output, severe corruption, or unusable positioning. |
| `Unsupported archive entries` | Archive opens, but it contains only media types this Amiga route does not currently load, such as ISO/CUE/BIN CD media. |
| `Unsupported media (format pending)` | Current build recognizes the media class but cannot mount it yet, such as IPF/CAPS floppy images before decoder support or HDF/HDZ images before hard-drive controller support. |

Quality gates will become stricter as the Amiga harness matures. Planned
columns for each proven cell include visual hash/region, rendered audio metrics,
input probe, save/load proof, disk-swap proof, and known fidelity notes.

The inventory is seeded from current local ADF/ADZ/IPF/HDF/archive launch
candidates under `D:\emu\amiga` and `D:\emu\amiga\adf`, with obvious
multi-disk sets collapsed to one row where the harness can identify the set.
Rows stay `TBD` until a model-specific smoke or parity probe proves them.

## Matrix

| Type / Title | Amiga 1000 | Amiga 500 | Amiga 2000 | Amiga 3000 | CDTV | Amiga 500+ | Amiga 600 | Amiga 1200 | Amiga 4000 | CD32 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **BIOS / Firmware** |  |  |  |  |  |  |  |  |  |  |
| Amiga 1000 Bootstrap 8 KiB | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amiga 1000 Bootstrap 64 KiB | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 0.7 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.0 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.1 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.2 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 | TBD | Operational (BIOS smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.4 beta | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 / 2.04 | TBD | TBD | TBD | TBD | TBD | Operational (BIOS smoke) | Operational (BIOS smoke) | TBD | TBD | TBD |
| Kickstart 2.05 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.0 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.5 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 4.0 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Amiga 1000 Bootstrap (USA, Europe) (64k) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Amiga 1000 Bootstrap (USA, Europe) (8k) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v1.2) (Rev 33.180) (A500, A2000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v1.3) (Rev 34.005) (A500, A2000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v1.4) (Rev 36.015) (Beta) (A500, A2000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v1.4) (Rev 36.016) (A3000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v2.04) (Rev 37.175) (A3000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v2.04) (Rev 37.175) (A500 Plus, A2000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v2.04) (Rev 37.175) (Proto) (A500 Plus, A2000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v2.05) (Rev 37.210) (Proto) (A600) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v2.05) (Rev 37.299) (A600) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v2.05) (Rev 37.300) (A600HD) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v2.05) (Rev 37.350) (A600HD) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.0) (Rev 39.065) (Beta) (A3000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.0) (Rev 39.106) (A1200) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.0) (Rev 39.106) (A4000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.055) (Beta) (A3000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.063) (A500, A600, A2000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.068) (A1200) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.068) (A3000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.068) (A4000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.068) (Proto) (A600) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.070) (A4000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| [BIOS] Kickstart (USA, Europe) (v3.1) (Rev 40.070) (A4000T) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 0.7 (27.3) (NTSC) (A1000) (Commodore) (1985) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.0 (NTSC) (A1000) (Commodore) (1985) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.0 (NTSC) (A1000) (Commodore) (1985) (disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.1 (31.34) (NTSC) (A1000) (Commodore) (1985) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.1 (31.34) (PAL) (A1000) (Commodore) (1986) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.1 (31.34) (PAL) (A1000) (Commodore) (1986) (disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.2 (33.166) (A1000) (Commodore) (1986) [!] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.2 (33.166) (A1000) (Commodore) (1986) [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.2 (33.166) (A1000) (Commodore) (1986) [b] (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.2 (33.166) (A1000) (Commodore) (1986) (disk) [!] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.2 (33.166) (A1000) (Commodore) (1986) (disk) [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.2 (33.180) (A500-A2000) (Commodore) (1986) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.1001) (CDTV) (Commodore) (1991) [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.5) (1000) (Commodore) (1987) (disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.5) (A500-A2500-A3000-CDTV) (Commodore) (1987)[!] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.5) (A500-A2500-A3000-CDTV) (Commodore) (1987) (early build) [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.5) (A500-A2500-A3000-CDTV) (Commodore) (1987) [h] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.5) (A500-A2500-A3000-CDTV) (Commodore-Cloanto) (1987) (encrypted) [h] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.5) (A500-A2500-A3000-CDTV) (Commodore-Cloanto) (1987) [h] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.3 (34.5) (A500-A2500-A3000-CDTV) (Guardian 1.2) (Commodore-Transactor) (1988) [h] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.4 (36.015) (A500-A2000) Alpha Release 15 (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.4 (36.02.20) (A3000) Alpha Release 18 (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 1.4 (36.02.20) (A3000) Alpha Release 18 (Commodore) (1989) (disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (36.143) (A3000) (Commodore) (1990) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (36.143) (A3000) (Commodore) (1990) (disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.175.20) Development (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.175.20) Development (Commodore) (1991) (disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.175) (A3000) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.175) (A3000) (Commodore) (1991) (disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.175) (A500+) (Commodore-Cloanto) (1991) (encrypted) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.210) (A2500) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.210) (A2500) (Commodore) (1991) [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.210) (A3000) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.210) (A3000) (Commodore) (1991) [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.299) (A600) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.300) (A600) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.0 (37.350) (A600) (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 2.04 (37.175) (A500+) (Commodore) (1991)[!] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.0 (39.106) (A1200) (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.0 (39.106) (A1200) (Commodore) (1992) (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.0 (39.106) (A4000) (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.0 (39.106) (A4000) (Commodore-Cloanto) (1992) (encrypted) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.055) (A3000) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.060) (CD32) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.063) (A600) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.068) (A1200) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.068) (A4000) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.068) (A4000) (Commodore-Cloanto) (1993) (encrypted) [h] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.068) (A4000) (Commodore-Cloanto) (1993) [h] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.068) (A4000) (ShapeShifter) (Commodore) (1993) [h] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.068) (A600) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.069) (A1200) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.1 (40.070) (A4000T) (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kickstart 3.5 (40.071) (OS3.5) (Commodore) (1996) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| **Applications** |  |  |  |  |  |  |  |  |  |  |
| A1060-A2088-A2286 Janus Setup Disk 1.3 German | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1060-A2088-A2286 Janus Setup Disk (1.3) (German) (Commodore) (1988) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1060 PC Sidecar Plus II System Disk | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1060 PC Sidecar Plus II System Disk (A1000) (Commodore) (1986) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1060 PC Sidecar System Disk | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1060 PC Sidecar System Disk (A1000) (Commodore) (1985) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1060 PC Sidecar Workbench 1.2 D | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1060 PC Sidecar Workbench 1.2 D (33.56) - (A1000) (Commodore) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1942 Monitor Setup Disk | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A1942 Monitor Setup Disk (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2010-A2020 Janus Test Diagnostics Disk 1.4 German | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2010-A2020 Janus Test Diagnostics Disk (1.4) (German) (Commodore) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2024 Monitor Setup Jumpstart Disk | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2024 Monitor Setup Jumpstart Disk [!] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2024 Monitor Setup 'Jumpstart' Disk (Commodore) (1988) [!] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2024 Monitor Setup 'Jumpstart' Disk (Commodore) (1988) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088-2286 PC Bridgeboard Install Disk 1.1 | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088-2286 PC Bridgeboard Install Disk (1.1) (A2000) (Commodore) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088-2286 PC Bridgeboard Install Disk 1.3 [!] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088-2286 PC Bridgeboard Install Disk (1.3) (A2000) (Commodore) (1988) [!] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088-2286 PC Bridgeboard Install Disk 2.0 [!] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088-2286 PC Bridgeboard Install Disk (2.0) (A2000) (Commodore) (1989) [!] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088T-2286 PC Bridgeboard Install Disk 1.02 | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2088T-2286 PC Bridgeboard Install Disk (1.02) (A2000) (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 A2000HD-A25000 HD ReInstall Disk 1.0 | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 A2000HD-A25000 HD ReInstall Disk (1.0) (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Hard-Disk Utilities (3.0) (A2000) (Commodore) (1989) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Hard-Disk Utilities 3.0 [m] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 HD Autoboot Disk (Commodore) (1988) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 HD Autoboot Disk [m] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 HD Install Disk | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 HD Install Disk (Commodore) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 HD Install Disk (Commodore) (1987) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 HD Install Disk [m] | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Miniscribe 8425 ReInstall Disk | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Miniscribe 8425 ReInstall Disk (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Miniscribe 8425 ReInstall Disk (Commodore) (1989) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Miniscribe 8425 ReInstall Disk [m] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Rodime RO3055 ReInstall Disk | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Rodime RO3055 ReInstall Disk (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Toshiba MK-134FA ReInstall Disk (Commodore) (1989) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2090 Toshiba MK-134FA ReInstall Disk [m] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2091 SCSI Controller Install Disk (1.27) (Commodore) (1990) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2091 SCSI Controller Install Disk 1.27 [m] | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2091 SCSI Controller Install Disk 1.3 | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2091 SCSI Controller Install Disk (1.3) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2232 Serial Board Install Disk | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2232 Serial Board Install Disk (A2000) (Commodore) (1990) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2320 Flicker-Fixer Test Disk | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2320 Flicker-Fixer Test Disk (Commodore) (1990) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2386 PC Bridgeboard Install Disk | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A2386 PC Bridgeboard Install Disk (A2000-A3000) (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 Burn-In Disk 1.0 | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 Burn-In Disk (1.0) (A3000) (Commodore) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 Burn-In Disk Final | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 Burn-In Disk (Final) (A3000) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 Multimedia Demo Disk 1 German | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 Multimedia Demo Disk 2 German | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 Multimedia Demo (German) (A3000) (DSP) (1990) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 PCBA Test Disk 1.0 | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A3000 PCBA Test Disk (1.0) (A3000) (Commodore) (1990) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A4091 SCSI Controller Install Disk | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A4091 SCSI Controller Install Disk (Commodore) (1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A570 CD Utility Disk (Commodore) (1990) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A570 CD Utility Disk [m] | TBD | Prompt only | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A590 HD RAM Test Disk 1.0 | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A590 HD RAM Test Disk (1.0) (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A590 HD RAM Test Disk (1.0) (Commodore) (1989) (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A590 HD RAM Test Disk 1.0 duplicate | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A590 HD Setup Disk (1.1) (Commodore) (1989) | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Action Replay (19xx)(ALE) | TBD | Prompt only | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Action Replay (19xx)(Datel Electronics)[cr Hackers Ethic] | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Action Replay 4 BETA (1993)(-)(AGA)[cr Paradox - Interpol] | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Amiga BASIC (1.0) (Commodore-Microsoft) (1985) | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Amiga BASIC (1.0) (Commodore-Microsoft) (1985) [m] | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Antheads - It Came from the Desert II Data Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Isle - Data Disk I - Air-Land-Sea (Germany) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Isle - Data Disk II - Der Mond von Chromos (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bloodwych - Data Disks Vol. 1 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Death or Glory - Das Erbe von Morgan (Germany) (HD Version) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Death or Glory - Der dunkle Kaiser (Germany) (HD Version) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Epic (Europe) (ECS) (Amiga 600 HD Bundles - Epic + Language Lab) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - Premier League Champions - 1994-95 Season Data Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Myth - History in the Making (Europe) (Amiga 600 HD Bundles - Epic + Language Lab) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProTracker v1.0b | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProTracker v1.0b (1990-09)(Amiga Freelancers)(PD)(m TITAN) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProTracker v2.0a | TBD | TBD | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| ProTracker v2.0a (1991-02)(Amiga Freelancers - 17-Bit Software)(PD) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProTracker v3.62 AGA | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProTracker v3.62 (AGA)(Y2K fixed)(Cryptoburners - RD10)(PD)(m TITAN) | TBD | Visual defect | TBD | TBD | TBD | Requirement/error prompt | Requirement/error prompt | TBD | TBD | TBD |
| ProTracker v4.0 Beta 2 AGA | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProTracker v4.0 Beta 2 (AGA)(Cryptoburners - RD10)(PD)(m TITAN) | TBD | Visual defect | TBD | TBD | TBD | Requirement/error prompt | Requirement/error prompt | TBD | TBD | TBD |
| Rome AD 92 - The Pathway to Power (Europe) (Amiga 600 HD Bundles - Epic + Language Lab) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strip Poker II+ Data Disk 1 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trivial Pursuit - The Language Laboratory Edition + Amiga Text (Europe) (ECS) (Amiga 600 HD Bundles - Epic + Language Lab) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vroom - Data Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wayne Gretzky Hockey - 1989 NHL Data Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.0 (30.xxx) (Commodore) (1985) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.0 (30.xxx) (Commodore) (1985) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.0 (30.xxx) (Commodore) (1985) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.1 (31.334) - Boot (Commodore) (1986) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.1 (31.334) - Boot (Commodore) (1986) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.1 (31.334) - Boot (Commodore) (1986) [m] [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.1 (31.334) - Boot (Commodore) (1986) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.1 (31.334) - Extras (Commodore) (1986) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.1 (31.334) - Extras (Commodore) (1986) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - Boot (Commodore) (1987) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - Boot (Commodore) (1987) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - Boot (Commodore) (1987) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - Extras (Commodore) (1987) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - Extras (Commodore) (1987) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - Extras (Commodore) (1987) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - The Very First (Tutorial-Juan Holz) (Horasoft-AHA) (1987) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - The Very First (Tutorial-Juan Holz) (Horasoft-AHA) (1987) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 (33.56) - The Very First (Tutorial-Juan Holz) (Horasoft-AHA) (1987) [m2] (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 D (33.56) - Boot (A2000) (Commodore) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.2 D (33.56) - Extras (A2000) (Commodore) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Boot (Commodore) (1988) (International) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Boot (Commodore) (1988) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Boot (Commodore) (1988) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Boot (Commodore) (1988) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Boot (Commodore) (1988) [m3] (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Extras (Commodore) (1988) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Extras (Commodore) (1988) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.2 (34.28) - Extras (Commodore) (1988) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 (34.34) - Boot (Commodore) (1991) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 (34.34) - Boot (Commodore) (1991) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 (34.34) - Boot (Commodore) (1991) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 (34.34) - Extras (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 (34.34) - Extras (Commodore) (1991) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 (34.34) - Extras (Commodore) (1991) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 D (34.34) - Boot (A500-A2000) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 D (34.34) - Boot (A500-A2000) (Commodore) (1991) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 D (34.34) - Extras (A500-A2000) (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3.3 D (34.34) - Extras (A500-A2000) (Commodore) (1991) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3 (34.20) - Boot (Commodore) (1988) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3 (34.20) - Boot (Commodore) (1988) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3 (34.20) - Extras (Commodore) (1988) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3 Boot | TBD | Operational (disk smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.3 - Tutorial (Gold Disk Inc.) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.4 (36.1) - Boot (A500-A2000) Alpha Release 15 (Commodore) (1989) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.4 (36.8) - Boot (A3000) Alpha Release Beta 1 (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.4 (36.8) - Extras (A3000) Alpha Release Beta 1 (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 1.4 (36.8) - Includes (A3000) Alpha Release Beta 1 (Commodore) (1989) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.0 (36.68) - Boot (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.0 (36.68) - Boot (Commodore) (1991) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.0 (36.68) - Extras (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.0 (36.68) - Fonts (Commodore) (1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.04 (37.67) - Boot (Commodore) (1992) | TBD | Visual defect | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Workbench 2.04 (37.67) - Boot (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.04 (37.67) - Boot (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.04 (37.67) - Extras (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.04 (37.67) - Fonts (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.04 (37.67) - Install (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Boot (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Boot (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Extras (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Extras (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Extras (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Fonts (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Fonts (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Fonts (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Install (A600HD) (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Install (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 (37.71) - Install (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.05 Boot | TBD | TBD | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Boot (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Boot (Commodore) (1992) (International) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Boot (Commodore) (1992) (International) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Extras (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Extras (Commodore) (1992) (International) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Fonts (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Fonts (Commodore) (1992) (International) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Install (Commodore) (1992) (International) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Locale (Commodore) (1992) (International) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.35) - Locale (Commodore) (1992) (International) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.36) - Boot (Commodore) (1992) | TBD | Prompt only | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Workbench 2.1 (38.36) - Extras (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.36) - Fonts (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.36) - Fonts (Commodore) (1992) (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.36) - Install (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 2.1 (38.36) - Locale (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Boot (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Boot (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Boot (Commodore) (1992) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Boot (Commodore) (1992) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Boot (Commodore) (1992) [m4] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Extras (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Extras (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Fonts (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Fonts (Commodore) (1992) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Fonts (Commodore) (1992) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Install (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Install (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Install (Commodore) (1992) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Locale (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Locale (Commodore) (1992) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.0 (39.29) - Storage (Commodore) (1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Boot (Commodore) (1994) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Boot (Commodore) (1994) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Boot (Commodore) (1994) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Boot (Commodore) (1994) [m4] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Boot (Commodore) (1994) [m5] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Boot (Commodore) (1994) [m5] [b] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Boot (Commodore) (1994) [m6] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Extras (Commodore) (1994) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Extras (Commodore) (1994) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Extras (Commodore) (1994) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Extras (Commodore) (1994) [m4] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Fonts (Commodore) (1994) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Fonts (Commodore) (1994) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Fonts (Commodore) (1994) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Fonts (Commodore) (1994) [m2] (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Fonts (Commodore) (1994) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Fonts (Commodore) (1994) [m4] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Install (Commodore) (1994) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Install (Commodore) (1994) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Install (Commodore) (1994) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Install (Commodore) (1994) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Install (Commodore) (1994) [m4] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Install (Commodore) (1994) [m5] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Locale (Commodore) (1994) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Locale (Commodore) (1994) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Locale (Commodore) (1994) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Locale (Commodore) (1994) [m4] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Locale (Commodore) (1994) [m5] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Storage (Commodore) (1994) [m] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Storage (Commodore) (1994) [m2] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Storage (Commodore) (1994) [m3] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Workbench 3.1 (40.42) - Storage (Commodore) (1994) [m4] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| X-Copy v5.21 USA Master Optimized | TBD | Operational (smoke) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| XTreme Racing - Data Disks (Europe) (v2.0) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| **Games** |  |  |  |  |  |  |  |  |  |  |
| 10 out of 10 - Early Essentials (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 10 out of 10 - Essential Science (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 10 out of 10 - Maths Number (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 1869 - Erlebte Geschichte Teil I (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 1869 - Erlebte Geschichte Teil I (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 1st Division Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 3D Construction Kit 2 (Europe) (r2.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 3D Construction Kit (Europe) (En,Fr,De,It) (r01.1000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 3D Galax (Europe) (Compilation - Action Amiga) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 3D Pool (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 3D World Boxing (Europe) (En,Fr,De,It) (Compilation - Amiga Sports Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 3D World Tennis (Europe) (En,Fr,De,It) (Compilation - Amiga Sports Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 4D Sports Boxing (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 4D Sports Driving (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 4th & Inches (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 4x4 Off-Road Racing (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 50 Great Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 500 C.C MotoManager (Europe) (En,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 5th Gear (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 688 Attack Sub (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 7 Colors (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 9 Lives (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A-10 Tank Killer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A.G.E. - Advanced Galactic Empire (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A-Train Construction Set (Europe) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A-Train Construction Set (Europe) (v1.00) (Compilation - Classics - A-Train) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A-Train (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A-Train (France) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A-Train (Germany) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| A320 Airbus Vol. 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| AAARGH! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Abandoned Places 2 (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Abandoned Places - A Time for Heroes (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Abandoned Places (Europe) (v1.17) (Demo) (Coverdisk - Amiga Action - Issue 30) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Abandoned Places - Zeit fuer Helden (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ABC Monday Night Football (USA) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Abracadabra (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Academy - Tau Ceti II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Accordion (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Action Fighter (Europe) (Amiga + PC) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Action Fighter (France) (Compilation - Les Fous du Volant) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Action Service (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Action Service (Europe) (Compilation - European Dreams) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Action Stations! (Europe) (v1.48) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Addams Family, The (Europe) (En,Fr,De) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Adidas Championship Tie-Break (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ADS - Advanced Destroyer Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ADS - Advanced Destroyer Simulator (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Champions of Krynn - A DragonLance Fantasy Role-Playing Epic, Vol.I (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Curse of the Azure Bonds - A Forgotten Realms Fantasy Role-Playing Epic, Vol.II (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Death Knights of Krynn - A DragonLance Fantasy Role-Playing Epic, Vol.II (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Dragons of Flame - A DragonLance Action Game (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - DragonStrike - DragonLance Dragon Combat Simulator (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Dungeon Masters Assistant - Volume I - Encounters (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Eye of the Beholder - A Legend Series Fantasy Role-Playing Saga, Vol.I (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Eye of the Beholder - A Legend Series Fantasy Role-Playing Saga, Vol.I (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Eye of the Beholder II - Legende von Darkmoon - A Legend Series Fantasy Role-Playing Saga, Vol.II (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Eye of the Beholder II - The Legend of Darkmoon - A Legend Series Fantasy Role-Playing Saga, Vol.II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Gateway to the Savage Frontier - A Savage Frontier Fantasy Role-Playing Epic, Vol.I (Europe) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Heroes of the Lance - A DragonLance Action Game (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Hillsfar - A Forgotten Realms Action Adventure (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Hillsfar - A Forgotten Realms Action Adventure (Germany) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Pool of Radiance - A Forgotten Realms Fantasy Role-Playing Epic, Vol.I (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Pools of Darkness - A Forgotten Realms Fantasy Role-Playing Epic, Vol.IV (Europe) (v1.0) (March 9,1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Secret of the Silver Blades - A Forgotten Realms Fantasy Role-Playing Epic, Vol.III (Europe) (v1.0) (June 12,1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Secret of the Silver Blades - A Forgotten Realms Fantasy Role-Playing Epic, Vol.III (Europe) (v1.0) (June 12,1991) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Shadow Sorcerer - A DragonLance Role-Playing Adventure Vol,1 (Europe) (UK-B 1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - The Dark Queen of Krynn - A DragonLance Fantasy Role-Playing Epic, Vol.III (Europe) (v1.0) (June 16, 1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Dungeons & Dragons - Treasures of the Savage Frontier - A Savage Frontier Fantasy Role-Playing Epic, Vol.II (USA) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Fruit Machine Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Ski Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advanced Ski Simulator (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Advantage Tennis (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Adventure Construction Set (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Adventures in Math (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Adventures of Quik and Silva, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Adventures of Robin Hood, The (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Adventures of Willy Beamish, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| African Raiders-01 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| After the War (Europe) (Compilation - Magnum) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Afterburner (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Afterburner (USA) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Agony (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Agony (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aigle d'Or, L' - Le Retour (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Air Bucks (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Air Bucks (Europe) (v1.2) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Air Support (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Air Support (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Airball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Airborne Ranger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| AirForce Commander (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Akira - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aladdin (Europe) (AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Albedo (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alcatraz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alert (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alfred Chicken - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alfred Chicken (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alfred Chicken (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alianator (Europe) (Coverdisk - Amiga Fun - Issue 12) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien 3 (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Alien Breed 3D - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Breed 3D (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Breed (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Alien Breed II - The Horror Continues (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Alien Breed II - The Horror Continues (Europe) (AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Alien Breed - Special Edition 92 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Breed Special Edition & Quak - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Breed Special Edition & Quak - CD32 (alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Breed - Tower Assault (Europe) (OCS, AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Alien Breed - Tower Assault (Europe) (v1.1) (OCS, AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Alien Drug Lords - The Chyropian Connection (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Fires 2199 AD (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Legion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Storm (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Syndrome | TBD | Visual defect | TBD | TBD | TBD | Requirement/error prompt | Requirement/error prompt | TBD | TBD | TBD |
| Alien Syndrome (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien Syndrome (USA) (Budget - Mindscape) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alien World (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aliex (Europe) (Trojan Phazer Required) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| All New World of Lemmings (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 'Allo 'Allo - Cartoon Fun! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 'Allo 'Allo - Cartoon Fun! (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alpha-1 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Alpha Waves (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Altered Beast (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Altered Destiny (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amazing Spider-Man, The and Captain America in Dr. Doom's Revenge! (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amazing Spider-Man, The (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ambermoon (Germany) (v1.01 22.10.1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amberstar (Europe) (v1.96) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amberstar (Germany) (v1.73) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amegas (Europe) (Compilation - TenStar Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amegas (Europe) (Compilation - TenStar Pack) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| American Tag-Team Wrestling (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amiga Encounter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amiga Karate (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amiga Spielesammlung - Band 1 (Germany) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Amnios (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Anarchy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ancient Art of War in the Skies, The (Europe) (v1.12) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Another World (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Another World (Europe) (Compilation - The Delphine Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Another World (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Anstoss - Die Fussball-Manager-Simulation (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Antago (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Apache + Overdrive (Europe) (Demo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| APB (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| APB (Europe) (Compilation - Tengen Arcade Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| APB + Xybots (Europe) (Compilation - TNT) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Apidya - CD32 | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Apidya (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Apidya (Germany) (En) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Apocalypse (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Apprentice (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Approach Trainer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aquatic Games Starring James Pond and the Aquabats, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aquaventura (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aquaventura (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arabian Nights - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arabian Nights (Europe) (En,Fr,De,It) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arcade Pool (Europe) (OCS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arcade Trivia Quiz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Archer Maclean's Pool (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Archipelagos (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Archipelagos (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Archon II - Adept (Europe) (Compilation - The Archon Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Archon II - Adept (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Archon - The Light and the Dark (Europe) (Compilation - The Archon Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Archon - The Light and the Dark (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arctic Fox (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arena (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arena (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arena (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arkanoid (Europe) (v1.04 2.28.88) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arkanoid [NTSC] | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Arkanoid - Revenge of Doh (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arkanoid - Revenge of Doh (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arkanoid (USA) (v1.05 1988-03-31) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arkanoid (USA) (v1.05 3.31.88) (AmigaDOS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Armada (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Armageddon Man, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Armalyte - The Final Run (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Armour-Geddon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Army Moves (Europe) (Coverdisk - CU Action - Issue May 1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arnhem - The 'Market Garden' Operation (Europe) (v1.0e) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arnie 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arnie (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Art of Chess, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Arthur | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Arthur [savegame] | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Arthur - The Quest for Excalibur (USA) (r54) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Artificial Dreams (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Artura (Europe) (Compilation - Action Amiga) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ashes (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Assassin (Europe) (v1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Assassin - Special Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Associated (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Astaroth - The Angel of Death (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Astate - La Malediction des Templiers (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Astatin (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Asterix im Morgenland (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Asterix - Operation Getafix (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Asterix und Operation Hinkelstein (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Atax (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ATF II - Advanced Tactical Fighter II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ATF II - Advanced Tactical Fighter II (Europe) (Budget - Action Sixteen) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Atomic Robo-Kid (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Atomino (Europe) (En,Fr,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Atomino (USA) (En,Fr,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Atomix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aufschwung Ost (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aunt Arctic Adventure (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Austerlitz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Australopiticus Mechanicus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| AutoDuel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| AV8B Harrier Assault (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Aventures de Moktar, Les (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Awesome (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Awesome (Europe) (Demo, Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Axe of Rage (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Axel's Magic Hammer (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Axel's Magic Hammer + Switchblade (Europe) (Compilation - 16 Bit Hit Machine) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B.A.T. (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B.A.T. (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B.A.T. (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B.A.T. II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B.A.T. II (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B.A.T. II (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B.C. Kid (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| B17 Flying Fortress (Europe) (En,Fr,De) (v1.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Baal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Baby Jo in 'Going Home' (Europe) (En,Fr,De,Es) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Back to the Future Part II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Back to the Future Part III (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Back to the Future Part III (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Backgammon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Backgammon Royale (Europe) (En,Fr,De) (v2.50 4.1.91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Backlash (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bad Cat (Europe) (Compilation - 5th Anniversary) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bad Company (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bad Dudes (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bad Dudes vs. Dragon Ninja (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Badlands (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Badlands (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Badlands Pete (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| BadLands + Skull & Crossbones + S.T.U.N. Runner (Europe) (Compilation - TNT 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Balance of Power - Geopolitics in the Nuclear Age (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Balance of Power - The 1990 Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ball Game, The (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ballistix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ballyhoo (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bambino Collection - Bambino and the Puzzle (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bandit Kings of Ancient China (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bangkok Knights (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Banshee - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Banshee (Europe) (En,Fr,De,Da) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bar Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Barbarian (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Barbarian (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Barbarian II (Europe) (Demo, Promo) (Psygnosis) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Barbarian II (Europe) (En,Fr,De,It) (Psygnosis) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Barbarian II (Europe) (Palace) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Barbarian II (Europe) (Palace) (Compilation - Heroes) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Barbarian II (Europe) (Palace) (Compilation - NRJ - La Compil'Action Vol.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Barbarian - The Ultimate Warrior (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Bard's Tale Construction Set, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bard's Tale II, The - The Destiny Knight (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bard's Tale III, The - Thief of Fate (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bard's Tale, The - Tales of the Unknown (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bard's Tale, The - Tales of the Unknown (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bargon Attack (France) (Compilation - Kings of Adventure 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Barney Bear Goes to Space (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Baron Baldric - A Grave Adventure (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Base Jumpers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Batman (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Batman (Europe) (1 Disk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Batman (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Batman Returns (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Batman - The Caped Crusader (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Batman - The Caped Crusader (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Bound (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Chess (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Chess (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Chess II - Chinese Chess (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Command (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Command (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle for the Ashes (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Isle (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Isle (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Squadron - The Destruction of the Barrax Empire! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Squadron - The Destruction of the Barrax Empire! (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Tec (Germany) (Coverdisk - Amiga Special - Issue xx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Valley (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battle Valley (Europe) (v3.0) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battlehawks 1942 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battlemaster (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battleships (Europe) (Budget - Encore) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battlestorm (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| BattleTech - The Crescent Hawk's Inception (USA) (v2.3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battletoads (1994)(Mindscape)[!] - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Battletoads (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beach Volley (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beam (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beambender (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beast Busters (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beastlord (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beavers - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beavers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Behind the Iron Gate (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beneath a Steel Sky - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beneath a Steel Sky (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beneath a Steel Sky (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beneath a Steel Sky (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Benefactor (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Bermuda Project (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bermuda Project (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Best of the Best - Championship Karate (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Betrayal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Better Dead Than Alien (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Better Dead Than Alien (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Better Maths (Europe) (Age 12 - 16) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beverly Hills Cop (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beyond Dark Castle (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beyond the Ice Palace + Battleships (Europe) (Compilation - The Story So Far - Vol 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beyond the Ice Palace (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beyond the Ice Palace (Europe) (Budget - Encore) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Beyond Zork - The Coconut of Quendor (USA) (r57) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bi-Fi Roll - Action in Hollywood (Germany) (v1.01) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bi-Fi Roll - SnackZone (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Big Business (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Big Business (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Big Run (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Biing! - Sex, Intrigen und Skalpelle (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Biing! - Sex, Intrigen und Skalpelle (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bill Elliott's NASCAR Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bill's Tomato Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bill's Tomato Game (Europe) (Demo, Promo) (Bundled with Cytron) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Billiards II Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Billiards Simulator (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bio Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bio Challenge (Europe) (Compilation - Light Force) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bionic Commando (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bionic Commando (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Birds of Prey (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bismarck (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bismarck (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Cauldron, The (Europe) (v2.00) (HLS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Crypt | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Black Crypt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Gold (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Gold (Germany) (v1.16 14.1.1992) (Compilation - World of Business) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Hornet (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Lamp (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Sect (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Shadow (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Shadow (Europe) (Compilation - Computer Hits - Volume Two) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Black Tiger (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| BlackCrypt Save | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blackjack Academy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blade (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blade Warrior (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blade Warrior (Europe) (Budget - Zeppelin Platinum) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blastaball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blastar (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blasteroids (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blazing Thunder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blinky's Scary School (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blitz Tennis (Europe) (OCS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blitzkrieg - Battle at the Ardennes (Europe) (v1.10) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blob (Europe) (Compilation - Corkers) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Block Out (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blockbuster (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blood Money (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| BloodNet - A Cyberpunk Gothic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| BloodNet - A Cyberpunk Gothic (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bloodwych (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bloodwych (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bloodwych (Europe) (Compilation - The Power Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blue Angel 69 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blue Angels - Formation Flight Simulation (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blue Max - Aces of the Great War (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blues Brothers, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Blues Brothers, The (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| BMX Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bo Jackson Baseball (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bob's Bad Day (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bobo (Europe) (Compilation - European Dreams) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bodo Illgner's Super Soccer (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Body Blows (Europe) (AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Body Blows (Europe) (v2.0) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Body Blows Galactic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Body Blows Galactic (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bomb Jack (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Bomb Jack + ThunderCats (Europe) (Compilation - Thrill Time - Platinum 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bomber Bob (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bombuzal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bombuzal (Europe) (Compilation - Brainblasters) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bombuzal (Europe) (Coverdisk - Amiga Power - Issue 01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bonanza Bros. (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Booly (Europe) (En,Fr) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Boot, Das (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Boot Disk | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Borobodur - Planet of Doom (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Borodino (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Borrowed Time (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bosse des Maths, La - 3 eme (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Boston Bomb Club (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Botics (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Boulder Dash Construction Kit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bozuma (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brain Blasters (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brainball (Europe) (Coverdisk - Amiga Mania - Issue March '92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bram Stoker's Dracula (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brat (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brataccas (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bravo Romeo Delta (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Breach 2 (Europe) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Breach (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Breathless (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brian Lara's Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brian the Lion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brian the Lion (Europe) (Demo, Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brian the Lion (Europe) (En,Fr,De,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brides of Dracula (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bridge 5.0 (Europe) (v5.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bridge 6.0 (Europe) (r2.49b) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bridge Player Galactica 2150 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brutal Football - Deluxe Edition (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brutal Football (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brutal - Paws of Fury (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brutal Sports Football - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubba 'n' Stix - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubba 'n' Stix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble And Squeak - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble and Squeak (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble and Squeak (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble Bobble | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Bubble Bobble (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble Bobble (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble Bobble (Europe) (Compilation - Jatte Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble Bobble (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble Dizzy (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble + (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble Ghost (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bubble Ghost (Europe) (Compilation - Super Quintet) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Buck Rogers Countdown to Doomsday | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Buck Rogers - Countdown to Doomsday - Science Fiction Role-Playing Computer Game, Vol.I (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Buck Rogers - Countdown to Doomsday - Science Fiction Role-Playing Computer Game, Vol.I (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Budokan | TBD | Visual defect | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| Budokan - The Martial Spirit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Buffalo Bill's Rodeo Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bug Bash (Europe) (Compilation - Bug Bash + Nucleus) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bug Bomber (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Buggy Boy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Buggy Boy (Europe) (Compilation - TenStar Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Buggy Boy (France) (Compilation - Les Fous du Volant) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Builderland (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bully's Sporting Darts (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bump 'n' Burn (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bumpy's Arcade Fantasy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bundesliga 3000 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bundesliga Manager Hattrick (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bundesliga Manager Professional (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bunny Bricks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bureaucracy (USA) (r86) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Burning Rubber (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Burntime (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Burntime (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Burntime (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Burntime (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bush Buck - A Global Treasure Hunt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Bush Buck - Eine Weltweite Schatzsuche (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Butcher Hill (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cabal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cadaver + Cadaver - The Payoff (Europe) (En,Fr,De) (v1.03) (Budget - Kixx) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Cadaver (Europe) (En,Fr,De) (v0.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cadaver (Europe) (En,Fr,De) (v1.03) (Compilation - The Bitmap Brothers - Volume 1) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Cadaver - The Payoff (Europe) (En,Fr,De) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Caesar (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Calculation (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Calephar (Europe) (Proto) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| California Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| California Games (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| California Games II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Campaign II - 50 Years of Global Conflict (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Campaign - Tactical & Strategic War Simulation (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder 1993 Virgin DE cr TRSI | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Cannon Fodder (1993)(Virgin)(DE)[cr TRSI] | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Cannon Fodder 2 1994 Virgin cr PDX | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Cannon Fodder 2 (1994)(Virgin)[cr PDX] | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder 2 (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Cannon Fodder 2 (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder 2 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Fodder Plus! (Europe) (Coverdisk - Amiga Power - Issue 31) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cannon Soccer (Europe) (Coverdisk - Amiga Format - Issue 54) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cap'n Carnage (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Capital Punishment (Europe) (En,De) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Capone (Europe) (v1.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captain Blood (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captain Blood (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captain Dynamo (Europe) (17.7.92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captain Dynamo (Europe) (2.11.92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captain Fizz Meets the Blaster-Trons (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captain Planet and the Planeteers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captive (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captive (Europe) (1MB) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captive (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Captive II - Liberation (Europe) (OCS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carcharodon - White Sharks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cardiaxx (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cardiaxx (Europe) (Budget - Team 17) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carl Lewis Challenge, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carlos (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carrier Command (Europe) (Compilation - Virtual Reality - Vol. 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carrier Command (Europe) (v13.08.90) (Budget - Mirror Image) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carrier Command (Europe) (vA1.2 1988-08-19) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carthage (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Carthage (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CarVup (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castle Master + Castle Master II - The Crypt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castle Master (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castle Master (Europe) (En,Fr,De) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castle of Dr. Brain (Germany) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castle of Dr. Brain (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castle Warrior (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castles (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Castles (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Catch'em (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cattivik - The Videogame (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CaveMania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cavitas (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CD32-888 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CD32 Gamer Vol 06 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CD32 Gamer Vol 09 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CD32 Gamer Vol 13 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cedric And The Lost Sceptre - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Celtic Legends (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Centerbase - Science-Fiction Simulation (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Centrefold Squares (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Centurion - Defender of Rome (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Century (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Challenge Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Challenger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chamber of the Sci-Mutant Priestess (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chambers of Shaolin - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chambers of Shaolin (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chambers of Shaolin (Europe) (Compilation - The First Year) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chamonix Challenge (Europe) (Compilation - Super Quintet) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Champ, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Champion Driver (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Champion of the Raj (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Backgammon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Baseball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Golf - The Great Courses of the World - Volume I - Pebble Beach (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Golf - The Great Courses of the World - Volume I - Pebble Beach (Europe) (Budget - Prism Leisure) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Manager 2 + Season 96-97 Updates (Europe) (v1.57) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Manager '93 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Manager '94 Season Disk - 1993-'94 (Europe) (Data Update Disk) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Manager '94 Season Disk - End of Season '93-'94 (Europe) (Data Update Disk) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Manager Italia (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Championship Run (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chaos Engine 2, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chaos Engine 2, The (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chaos Engine, The (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Chaos Engine, The (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chaos in Andromeda - Eyes of the Eagle (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chariots of Wrath (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Charon 5 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chase H.Q. (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chase H.Q. (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chase H.Q. + Hard Drivin' (Europe) (Compilation - Wheels of Fire - The Ultimate Driving Compilation) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chase H.Q. II - Special Criminal Investigation (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chess Player 2150 (Europe) (Compilation - Arcade Action) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chess Player 2150 (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chess Player 2150 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chess Simulator (Europe) (En,Fr) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chessmaster 2000, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chessmaster 2100 (USA) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chicago 90 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chicago 90 + Highway Patrol 2 + Jump Jet + Phantasm (Europe) (Compilation - Speed Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chip's Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chip's Challenge (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Christoph Kolumbus (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Christoph Kolumbus (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chronicles of Omega, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chrono Quest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chrono Quest (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chrono Quest II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chuck Rock (1994)(Core)[!] - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chuck Rock 2 - Son of Chuck (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Chuck Rock (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Chuck Yeager's Advanced Flight Trainer 2.0 (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chuckie Egg (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Chuckie Egg II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Circus Attractions (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Circus Attractions (Europe) (Compilation - Milestones) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cisco Heat (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| City Defence + Fortress Underground (Europe) (Compilation - Powerbox) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| City Defence + Karate King (Europe) (Compilation - Sextett) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Civilization (Europe) (v855.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Civilization (Europe) (v855.04) (Compilation - Award Winners - Platinum Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Civilization (Europe) (v855e.01) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Civilization (Germany) (v855.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Civilization (Germany) (v855.04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CJ in the USA (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CJ's Elephant Antics (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Classic Arcadia + Baby Arcadia (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Classic Board Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cliffhanger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Clockwiser (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Clockwiser (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Clou!, Der (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Clou!, Der (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cloud Kingdoms + Kid Gloves (Europe) (Demo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Clown-O-Mania (Europe) (Budget - Top Shots) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Club Football - The Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Clue - Master Detective (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Clue!, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cluedo - Master Detective (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Coala (Europe) (ECS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Codename Hellfire - Armour-Geddon II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Codename - Iceman (Europe) (v1.036) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cogan's Run (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cohort II - Fighting for Rome (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Colonel's Bequest, The (Europe) (v1.000.059) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Colonization (Europe) (En,Fr,De) (v1.11) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Colony, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Colorado (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Colorado (Europe) (En,De) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Colossus Bridge 4 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Colossus Chess X - The Ultimate Chess Program (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Combat Air Patrol (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Combat Course (USA) (v1.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Combo Racer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Combo Racer (Europe) (Budget - GBH) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Commando (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Computer Third Reich (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conan - The Cimmerian (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conflict - Europe (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conflict - Europe (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conflict - Korea (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conflict - Middle East - Arab-Israeli Wars - 1973-x (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conqueror (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conqueror (Europe) (En,De) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conqueror (Europe) (En,De) (Compilation - Mega Box) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Conquests of Camelot - The Search for the Grail (Europe) (v1.009) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Continental Circus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cool Croc Twins (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cool Spot (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cool World (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Corporation (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Corporation (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Corporation - Mission Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Corruption (Europe) (v1.11) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cortex (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Corx (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cosmic Bouncer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cosmic Relief (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cosmic Spacehead (Europe) (En,Fr,De,Es) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cosmostruction (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Count Duckula 2 Featuring Tremendous Terence (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Count Duckula in No Sax Please - We're Egyptian (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cover Girl Poker (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Covert Action (Europe) (v447.101) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Covert Action (Germany) (v447.101) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crack Down (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crack (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Craps Academy - Micro-Vice Series (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crash Garrett (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crazy Cars 3 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crazy Cars (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crazy Cars (Europe) (Compilation - Action d'Enfer) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crazy Cars II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crazy Football (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crazy Shot (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crazy Sue II (Europe) (Coverdisk - Amiga Mania - Issue September '92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Creature (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Creatures (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Cribbage King + Gin King (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cricket Captain (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crime Does Not Pay (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crime Wave (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Croisiere pour un Cadavre (France) (v1.04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cross Out the Intruder (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| CrossCheck - Eishockey Action (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crown (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cruise for a Corpse (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cruise for a Corpse (Europe) (Compilation - The Delphine Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cruise for a Corpse (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crystal Dragon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crystal Hammer + Final Mission (Europe) (Compilation - Amiga Star Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crystal Kingdom Dizzy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crystals of Arborea (France) (Compilation - Magic Worlds) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Crystals of Arborea (Germany) (Budget - Bit Star) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cubulus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cubulus & Magic Serpent (1991)(Software 2000)(De) - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Curse of Enchantia (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Curse of Ra, The (Europe) (v1.4b) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Custodian (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cutthroats (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cyber World (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cyberball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cyberball (Europe) (Compilation - Winning Team) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cyberblast (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cybercon III (Europe) (4.6.91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cybercon III (Europe) (8.6.91) (Budget - Kixx XL) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cybercop (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cybernauts (Europe) (Compilation - Powerbox) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cybernoid II - The Revenge (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Cybernoid - The Fighting Machine (Europe) (Compilation - Action Amiga) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Cyberpunks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cycles, The - International Grand Prix Racing (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Cytron (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| D-Day - The Beginning of the End (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| D-Generation (1993)(Mindscape)[!] - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| D-Generation (1993)(Mindscape)[!] - CD32 (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| D-Generation (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| D-Generation (Europe) (v1.05) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Daily Double Horse Racing (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dalek Attack (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dalek Attack (Europe) (Compilation - The Sci-Fi Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Daley Thompson's Olympic Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Daley Thompson's Olympic Challenge (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Damocles - Mercenary II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Damocles - Mercenary II - Mission Disk 1 (Europe) (En,Fr,De) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Damocles - Mercenary II - Mission Disk 2 (Europe) (En,Fr,De) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Damocles - Mercenary II (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dan Dare III - The Escape (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Danger Freak (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dangerous Streets (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dangerous Streets (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Darius+ (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Dark Castle (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dark Century (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dark Fusion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dark Fusion + Steel + Turbo Trax (Europe) (Compilation - Turbo Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dark Side (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dark Side (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dark Spyre (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Darkman (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Darkmere (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Darkseed - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Darkseed (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Datastorm (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Datastorm (Europe) (Compilation - Astra Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dawn Patrol (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dawn Patrol (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Day of the Pharaoh (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Day of the Pharaoh (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Day of the Viper (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Days of Thunder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Death Bringer (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Death Mask (Europe) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Death Trap (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deathbringer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Debut - Planet Simulation (Europe) (v1.05) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deep Core (1993)(ICE) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Deep Core (1993)(ICE)(alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deep Core (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deep Space (Europe) (v1.00a) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deep Space (USA) (v1.00a) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deep, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Def Con 5 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Defender II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Defender of The Crown 2 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Defender of the Crown (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Defender of the Crown (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Defenders of the Earth (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deflektor (Europe) (Compilation - Action Amiga) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deja Vu - A Nightmare Comes True! (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deliverance (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deluxe Monopoly (Europe) (Compilation - Board Genius) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deluxe Strip Poker 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deluxe Strip Poker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Demolition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Demon's Tomb - The Awakening (Europe) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Demon's Winter (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Demon Wars (Europe) (Coverdisk - Amiga Fun - Issue 04) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Demoniak (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Denaris (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Denaris (Europe) (Compilation - 5th Anniversary) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dennis (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dennis (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dennis (Europe) (AGA) (Amiga 1200 Bundle - Desktop Dynamite) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Denver Presente - Je Decouvre les Couleurs (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Desert Strike | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Desert Strike-3 (1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Desert Strike - Return to the Gulf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Destroyer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Detroit (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deuteros - The Next Millennium (Europe) (En,Fr,De) (v1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Deuteros - The Next Millennium (Europe) (En,Fr,De) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Devious Designs (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dick Tracy (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dick Tracy - The Crime-Solving Adventure (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Die Hard 2 - Die Harder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Diggers (Europe) (En,Fr,De,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dimo's Quest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dingsda (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dino Wars (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dino Wars (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dinosaur Detective Agency (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Disc (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Disc (Europe) (Budget - Action Sixteen) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Disc (Europe) (Compilation - Podium) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Discovery - In the Steps of Columbus (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Disk-O-Rogue | TBD | Visual defect | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Disposable Hero (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Distant Armies - A Playing History of Chess (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dizzy Collection - 5 Game Pack (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dizzy Dice (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dizzy Panic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dizzy - Prince of the Yolkfolk (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Dizzy's Excellent Adventures (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| DM2 Skullkeep | TBD | Prompt only | TBD | TBD | TBD | Prompt only | Prompt only | TBD | TBD | TBD |
| DNA Warrior (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dogfight - 80 Years of Aerial Warfare (Europe) (En,Fr,De,It) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dogs of War (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Dojo Dan (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dominator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dominium (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Donald's Alphabet Chase (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Donk! - The Samurai Duck! (Europe) (OCS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Doodlebug (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Doofus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Double Dragon (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Double Dragon (Europe) (v2.16) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Double Dragon II - The Revenge (Europe) (v3.84) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Double Dragon II - The Revenge (Europe) (v4.01) (OCS, ECS) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Double Dragon II - The Revenge (USA) (v4.16ecs) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Double Dragon III - The Rosetta Stone (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Down at the Trolls (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dr. Doom's Revenge! (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dr. Plummet's House of Flux (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon Breed (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon Lord (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon's Lair - Escape from Singe's Castle (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon's Lair - Escape from Singe's Castle (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon's Lair (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon's Lair II - Time Warp (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon's Lair III - The Curse of Mordread (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon Spirit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon Spirit (Europe) (Compilation - TNT) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon Wars (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragon Wars (Germany) (v1.2-G) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragonflight (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragonflight (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragonflight (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragonflight (Germany) (0058) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragons Breath (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragons Breath (France) (Compilation - Magic Worlds) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragons Breath (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragons Breath (Germany) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragonstone - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dragonstone (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drakkhen (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drakkhen (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drakkhen (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drakkhen (USA) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dreadnoughts (Europe) (v1.11) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| DreamWeb (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| DreamWeb (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| DreamWeb (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Driller (Europe) (Budget - Kixx) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Driller (Europe) (En,Fr,De,It) (v1.0) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Driller + Total Eclipse (Europe) (Compilation - Virtual Worlds) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drivin' Force (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drivin' Force (Europe) (1504) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drivin' Force (Europe) (Compilation - Powerplay) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Drole d'Ecole (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| DuckTales - The Quest for Gold (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| DuckTales - The Quest for Gold (Europe) (0299) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dugger - Starring Herbie Stone (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dune (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dune (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dune (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dune II | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Dune II - Battle for Arrakis (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dune (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dungeon Master | TBD | Requirement/error prompt | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Dungeon Master (Europe) (En,Fr,De) (v3.6) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dungeon Master II - The Legend of Skullkeep (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dungeon Quest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dungeons, Amethysts, Alchemists 'n' Everythin' (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dungeons of Avalon (Europe) (Coverdisk - Amiga Mania - Issue July '92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dungeons of Avalon II - The Island of Darkness (Europe) (Coverdisk - Amiga Mania - Issue October '92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dylan Dog 12 - Il Lungo Addio (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dylan Dog 13 - I Killers Venuti dal Buio (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dylan Dog 2 - Ritorno al Crepuscolo (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dylan Dog 3 - Storia di Nessuno (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dylan Dog 4 - Ombre (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dylan Dog 5 - La Mummia (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dylan Dog 6 - Maelstrom (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dyna Blaster (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dynamite Dux (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dynamite Dux (Europe) (Compilation - Sega Master Mix) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dynasty Wars (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dynatech (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Dyter-07 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| E-Motion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| E.S.S. - European Space Simulator (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eagle's Rider (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eagles Nest (Europe) (Budget - Smash 16) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Earl Weaver Baseball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| East vs. West Berlin 1948 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ebonstar (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ECO (Europe) (Budget - CU Amiga-64 - November 1989) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Eco Phantoms (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Edd the Duck 2 - Back with a Quack! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Edd the Duck! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eishockey Manager (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eishockey Manager - Limited Edition (Germany) (v1.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Electronic Pool (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elf (Europe) (En,Fr,De) (Ocean) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elf (Europe) (MicroValue) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elfmania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eliminator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elite (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Elite (Europe) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elite (Europe) (v2.0) (Compilation - Space Legends) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elvira II - The Jaws of Cerberus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elvira II - The Jaws of Cerberus (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elvira II - The Jaws of Cerberus (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elvira - Mistress of the Dark (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elvira - Mistress of the Dark (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Elvira - Mistress of the Dark (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Embryo (Europe) (En,Fr,Hr) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Emerald Mine | TBD | Operational (visual smoke) | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| Emerald Mine 3 - Professional (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Emerald Mine 3 - Professional (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Emerald Mine (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Emerald Mine II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EmeraldMinesCD32CUE | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Emlyn Hughes International Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Emmanuelle (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Emmanuelle (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Empire Soccer '94 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Enchanted Land (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Encyclopedia of War - Ancient Battles (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Energie-Manager (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Enforcer, The (Europe) (Trojan Phazer Required) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| England Championship Special (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Enlightenment - Druid II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Enterprise (Europe) (Note - Mastered with virus) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Entity (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Epic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Equality (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Erbe, Das (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Erben der Erde - Die Grosse Suche (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Erben der Erde - Die Grosse Suche (Germany) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Escape from Colditz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Escape from the Planet of the Robot Monsters (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Escape from the Planet of the Robot Monsters (Europe) (Compilation - Tengen Arcade Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eskimo Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Espana - The Games '92 (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Espionage (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ESWAT (Europe) (Compilation - Super Sega) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Euro Soccer '88 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Euro Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| European Champions (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| European Championship 1992 (Europe) (En,Fr,De,Es,It,Sv) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| European Football Champ (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| European Soccer Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| European Superleague (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Evil Garden (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Excalibur (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Excellent Card Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exile - Discovery Disk (Europe) (En,Fr,De) (AGA) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exile (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exile (Europe) (En,Fr,De) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exodus 3010 - The First Chapter (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exolon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exolon (Europe) (Budget - Black Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Explora II (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Explora III - Sous le Signe du Serpent (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Extase (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exterminator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Extractors (Diggers2) Preview - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eye of Horus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Eye of Horus (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F-15 Strike Eagle II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F-16 Combat Pilot (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F-19 Stealth Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F-19 Stealth Fighter (Europe) (Compilation - Combat Classics 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F-19 Stealth Fighter (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F-29 Retaliator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F1 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F1 GP Circuits (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F1 World Championship Edition (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| F17 Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| FA-18 Interceptor (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Face-Off Ice Hockey (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Face-Off Ice Hockey (Europe) (En,Fr,De,Es,It) (1670) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Faery Tale Adventure, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon (Europe) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon Mission Disk - Operation Counterstrike (USA) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon Mission Disk - Operation Firefight (USA) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon - Mission Disk Vol. 1 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon - Mission Disk Vol. II (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon (USA) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Falcon (USA) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fantastic Dizzy (Europe) (En,Ja,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fantastic Voyage - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fantastic Voyage (USA, Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fantasy Manager - The Computer Game (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fantasy World Dizzy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fascination (France) (Compilation - Kings of Adventure 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fast Break (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fast Food (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fast Lane (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fatal Heritage (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fate - Gates of Dawn (Europe) (v1.6) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fate - Gates of Dawn (Germany) (v1.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fate - Gates of Dawn (Germany) (v1.7) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fatman - The Caped Consumer (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fatman - The Caped Consumer (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fears - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fears (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fears (Europe) (Preview) (AGA) (Coverdisk - CU Amiga - Issue October 1995) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Federation Quest 1 - B.S.S. Jane Seymour (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Federation (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fernandez Must Die (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ferrari Formula One (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Feud (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Feudal Lords (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fields Of Glory - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fields of Glory (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fiendish Freddy's Big Top o' Fun (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| FIFA International Soccer (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fighter Bomber - Advanced Mission Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fighter Bomber (Europe) (Compilation - Power Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fighter Bomber (Europe) (En,Fr) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fighter Duel Pro (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fighting Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Assault (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Battle, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Battle, The (France) (En,Fr) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Blow (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Command (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Command (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Countdown (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Fight (Europe) (Compilation - Super Fighter) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Final Mission (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fire and Brimstone (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fire and Forget (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fire-Brigade - The Battle for Kiev 1943 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fire Force - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fire Force (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fire & Forget II - The Death Convoy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fire & Ice - The Daring Adventures of Cool Coyote (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Fireball + Balls + Space Bomber 3 (Germany) (Coverdisk - Amiga Fever - Issue xx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fireblaster (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Firehawk (Europe) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Firepower (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Firestar (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| FireTeam 2200 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| FireZone (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| FireZone (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| First Contact (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| First Person Pinball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| First Samurai (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| First Samurai (Europe) (Demo) (Compilation - Double Confrontation) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fish! (Europe) (v1.03) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fist Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flamingo Tours (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flashback (Europe) (1993-04-22) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flashback (Europe) (Compilation - The Delphine Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flashback (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flashback (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flies - Attack on Earth (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight of the Amazon Queen (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight of the Intruder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Path 737 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Path 737 (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Simulator II (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Simulator II (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Simulator II - Scenery Disk 11 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Simulator II - Scenery Disk 7 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Simulator II - Scenery Disk - Western European Tour (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flight Simulator II (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flimbo's Quest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flink - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flintstones, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flip-it & Magnose - Water Carriers from Mars (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Flood (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fly Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fly Harder - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fly Harder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| FOFT - Federation of Free Traders (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fool's Errand, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Director II (Europe) (En,Fr,De,Es,It) (v2.06D) (CDS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Glory (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Manager 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Manager 2 - Expansion Kit (Europe) (Bundled With FM2) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Manager 2 - Expansion Kit (Europe) (Compilation - Amiga Sports Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Manager 2 + Football Manager - World Cup Edition (Europe) (Compilation - Soccer Mania) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Manager - World Cup Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Manager - World Cup Edition (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Football Masters (Europe) (v5.13) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Footballer of the Year 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| FootMan (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Forgotten Worlds (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Formula 1 3D (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Formula 1 Grand Prix (Europe) (Compilation - Mega Pack II) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Foundation's Waste (Europe) (Amiga + ST) (Coverdisk - The One - Issue 22) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Foundation's Waste (Europe) (Budget - PC Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Four Crystals of Trazere, The (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Franco Baresi World Cup - Kick Off (Italy) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frankenstein (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fred Feuerstein (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Freedom - Die Krieger des Schattens (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Freedom - Les Guerriers de l'Ombre (France) (Compilation - Les Enfants du Silence) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frenetic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frontier Elite 2 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frontier Elite 2 (EUR) - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frontier - Elite II (Europe) (06.10.1993) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frontier - Elite II (Europe) (2.9.1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frontier - Elite II (Europe) (v1.05r4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frontier - Elite II (France) (02.09.1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frontier - Elite II (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Frost Byte (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Full Contact (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Full Metal Planete (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Full Metal Planete (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Full Metal Planete (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fun School 2 - For the Over-8s (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fun School 2 - For the Under-6s (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fun School 3 - CDTV & CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fun School 3 - For 5 to 7 Year Olds (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fun School Specials - Spelling Fair (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fury of the Furries (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fusion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fussball Total (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Basketball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Bike Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Classics Collection (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Shock (Europe) (Coverdisk - Amiga Fun - Issue 05) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Space (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Wars - Time Travellers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Wars - Time Travellers (Europe) (Compilation - The Delphine Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Wars - Time Travellers (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Wars - Time Travellers (Spain) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Future Wars - Time Travellers (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Fuzzball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| G-LOC - R360 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| G.nius (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| G.P. Tennis Manager (Europe) (En,It) (v4 18.3.91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galactic Conqueror (Europe) (Compilation - Titus Action II) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galactic Empire (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galactic Empire (Europe) (Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galactic Invasion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galactic Warrior Rats (Europe) (Compilation - The Sci-Fi Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galaxy Blast (Europe) (Coverdisk - Amiga Mania - Issue August '92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galaxy Force II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Galdregon's Domain (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gamers Delight - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Games, The - Summer Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Games, The - Summer Edition (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Games, The - Winter Edition (Europe) (Compilation - Sporting Gold) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Games, The - Winter Edition (Europe) (Compilation - Winter Gold) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Garfield - Big, Fat, Hairy Deal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Garrison (Europe) (v1.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Garrison II (Europe) (v1.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gary Lineker's Hot-Shot! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gauntlet II | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Gauntlet II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gauntlet III - The Final Quest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gazza II (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gazza's Super Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| GBA Championship Basketball - Two-on-Two (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gear Works (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gem'X (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gemini Wing (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Genesia (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Genghis Khan (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Germ Crazy (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Get Out (Europe) (Coverdisk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gettysburg (Europe) (Compilation - Turning Points) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| GFL Championship Baseball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| GFL Championship Football (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ghostbusters II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ghostbusters II (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ghosts'n Goblins (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ghouls 'n' Ghosts (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ghouls 'n' Ghosts (Europe) (Compilation - Platinum) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ghouls 'n' Ghosts + Venus - The Flytrap (Europe) (Compilation - Chart Attack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gilbert - Escape from Drill (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Global Commander (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Global Effect - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Global Effect (Europe) (En,Fr,De,It) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Global Gladiators (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Globdule (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gloom - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gloom Deluxe (Europe) (ECS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gloom (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gluecksrad (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gnome Ranger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Goal! (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Goal! (Europe) (En,Fr,De,Es,It,No) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gobliiins (Europe) (En,Fr,De,Es,It) (Compilation - Kings of Adventure 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gobliiins (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gobliins 2 - The Prince Buffoon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gobliins 2 - The Prince Buffoon (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gobliins 2 - The Prince Buffoon (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Goblins 3 (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Goblins 3 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Godfather, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gods (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Gods (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gold of the Aztecs, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gold Rush! (Europe) (v2.05) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Golden Axe | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Golden Axe AGA | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Golden Axe (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Golden Oldies Vol. 1 (Europe) (v2.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Golden Path (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Golden Path (Europe) (Compilation - Computer Hits - Volume Two) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Goldrunner (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Goldrunner II (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Goofy's Railway Express (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gotcha! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Graeme Souness Vector Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Graffiti Man (Europe) (Compilation - 5th Anniversary) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Graham Gooch's Second Innings (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Graham Gooch World Class Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Graham Taylor's Soccer Challenge (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grail, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grand Monster Slam (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grand Monster Slam (Europe) (Compilation - Milestones) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grand National (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grand Prix Circuit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grand Prix Master (Europe) (Compilation - Amiga Sports Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grand Prix Master (France) (Compilation - Les Fous du Volant) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grand Slam (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gravity (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gravity Force (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Great Courts 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Great Courts 2 (Europe) (Compilation - Commodore Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Great Courts (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Great Giana Sisters, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Great Giana Sisters, The (Europe) (First Release) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Great Napoleonic Battles (Europe) (En,Fr,De,Es,It) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Greens - The Ultimate 3-D Golf Simulation (Europe) (v2.3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gremlins 2 - The New Batch (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gremlins 2 - The New Batch (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grid Start (Europe) (Compilation - Sextett) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gridiron! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grimblood (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Grimblood (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Growth (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Guardian Angel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Guardian (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Guild of Thieves, The (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Guldkorn Expressen (Denmark) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gulp - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gulp! (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gunboat - River Combat Simulation (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gunship 2000 (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gunship 2000 (Europe) (v3.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gunship (Europe) (v832.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gunship (USA) (v832.03) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Gunshoot (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Guy Spy and the Crystals of Armageddon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| H.A.T.E. - Hostile All Terrain Encounter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hacker II - The Doomsday Papers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hacker II - The Doomsday Papers (Europe) (Compilation - Power Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hacker (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hagar (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hagar the Horrible (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Halley Project, The - A Mission in Our Solar System (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hammer Boy (Europe) (Budget - PC Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hammerfist (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hanse - Die Expedition (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hard Drivin' (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hard Drivin' II - Drive Harder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hard Drivin' II - Drive Harder (Europe) (Compilation - Tengen Arcade Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hard Drivin' II - Drive Harder + Hydra (Europe) (Compilation - TNT 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hard Drivin' + Toobin' (Europe) (Compilation - TNT) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hard 'n' Heavy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hard Nova (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| HardBall! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| HardBall II (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hare Raising Havoc (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harlequin (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harlequin - Special Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harley-Davidson - The Road to Sturgis (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harpoon + Battleset 1 - Showdown in the North Atlantic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harpoon - Battleset 2 - The North Atlantic Convoys (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harpoon - Battleset 3 - The MED Conflict (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harpoon - Battleset 4 - The Indian Ocean (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Harrier Combat Simulator (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hawkeye (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Head over Heels (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heart of China (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heart of China (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heart of the Dragon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heavy Metal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heimdall 2 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heimdall 2 (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heimdall 2 (Europe) (En,Fr,De,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Heimdall (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Helicopter Mission (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hell Raiser (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hellbent (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hellraider (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hellrun Machine, The (Europe) (Coverdisk - Amiga Mania - Issue November '92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Helter Skelter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Henrietta's Book of Spells (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hero Quest 2 - Legacy of Sorasil (1994) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hero Quest 2 - Legacy of Sorasil (RAR) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hero's Quest - So You Want To Be A Hero (Europe) (v1.134) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| HeroQuest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| HeroQuest II - Legacy of Sorasil (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| HeroQuest - Return of the Witch Lord (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hexuma - Das Auge des Kal (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hi TEC Hanna-Barbera Cartoon Character Collection, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| High Seas Trader (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| High Steel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Highway 42 + Warlords (Germany) (Coverdisk - Amiga Software Extra Nr. 12 - Disk 1 of 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Highway Hawks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Highway Patrol 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hill Street Blues (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hill Street Blues (Europe) (En,Fr,De,Es,It) (Budget - Buzz) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hired Guns (Europe) (En,Fr,De,Es,It) (v1.08.39.25) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Historyline 1914-1918 (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Historyline 1914-1918 (France) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Historyline 1914-1918 (Germany) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hoi (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hole-In-One - Miniature Golf (Europe) (Amiga 500 Bundle - Starter Kit) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Holiday Lemmings 1993 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Holiday Lemmings 1994 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hollywood Hijinx (USA) (r37) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hollywood Pictures - Der Kinomanager (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hollywood Poker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hollywood Poker (Europe) (Budget - Smash 16) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hollywood Poker Pro (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hong Kong Phooey (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hook (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hopp oder Topp (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Horror Zombies from the Crypt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hostage - Rescue Mission (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hostages (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hostile Breed (Europe) (En,Fr,De) (Unreleased) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hot Rod (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| HotBall (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hotshot (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hotshot (Europe) (Compilation - Hits for Six - Volume Two) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hound of Shadow, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hoverforce (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hoversprint (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hoyle Official Book of Games - Volume 1 (Europe) (v1.000.139) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hoyle Official Book of Games - Volume 2 - Solitaire (Europe) (v1.001.016 7.17.90) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hoyle Official Book of Games - Volume 3 - Great Board Games (Europe) (v1.000 3.25.92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Huckleberry Hound in Hollywood Capers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hudson Hawk (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Huey (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hugo 2 (Finland) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hugo (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hugo - Pa Nye Eventyr 2 (Denmark) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hugo - Pa Nye Eventyr (Denmark) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hugo (Sweden) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Human Race - The Jurassic Levels (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Human Race - The Jurassic Levels (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Humans III - Evolution - Lost in Time (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Humans, The (Europe) (Compilation - Help!) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Humans, The (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Humans, The (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hungary for Fun - Logic + Zarcan + Kid Pool (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hunt for Red October, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hunt for Red October, The - The Movie (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hunter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hunter Killer (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hybris (USA) (v0.95) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hydra (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hyperdome (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hyperforce + Artificial Dreams (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Hyperion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| I Play - 3-D Soccer (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ice Hockey aka Face Off (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| IK+ (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| IK+ (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ikari Warriors + Buggy Boy (Europe) (Compilation - The Story So Far - Vol 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ikari Warriors (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ikari Warriors (Europe) (Compilation - TenStar Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Illustrated Works of Shakespeare, The (1990) - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ilyad (Europe) (Coverdisk - Amiga Action - Issue 20) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Immortal, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Impact (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Imperium (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Impossamole (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Impossible Mission 2025 - The Special Edition (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Impossible Mission 2025 - The Special Edition (Europe) (En,Fr,De,Es,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Impossible Mission II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| In 80 Days Around the World (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Incredible Crash Dummies, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Fate of Atlantis | TBD | Visual defect | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| Indiana Jones and the Fate of Atlantis - A Graphic Adventure (Europe) (v1.06 10-12-92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Fate of Atlantis - A Graphic Adventure (France) (v1.0 3-2-93) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Fate of Atlantis - Ein Grafik Adventure (Germany) (v1.0 1992-10-28) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Fate of Atlantis - The Action Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Last Crusade - Das Graphic Adventure (Germany) (v1.4 1989-10-20) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Last Crusade - The Action Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Last Crusade - The Graphic Adventure (France) (v1.4 10.6.89) (Compilation - Fun Radio - La Compil Micro 3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Last Crusade - The Graphic Adventure (Italy) (v1.5 29.12.89) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Last Crusade - The Graphic Adventure (USA) (v1.4 10.4.89) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indiana Jones and the Temple of Doom (Europe) (v3.28) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indianapolis 500 - The Simulation (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Indy Heat (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Infestation (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Infidel (USA) (r22) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Innocent Until Caught (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Insanity Fight (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Insector Hecti in the Inter Change (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Insects in Space (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Inspektor Griffu - Ein Toter hat Heimweh (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International 3D Tennis (Europe) (Compilation - Super Sim Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Championship Athletics (Europe) (En,Fr,De,Es,It) (16.2.1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Championship Athletics (Europe) (En,Fr,De,Es,It) (9.6.1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Championship Wrestling (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Ice Hockey (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Karate + (System 3)(1994) - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International One Day Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Soccer Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Soccer Challenge (Europe) (Compilation - Virtual Reality - Vol. 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| International Sports Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Interphase (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Intrigue a la Renaissance (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Intrigue + Silhouette (Europe) (Compilation - Amiga Complete) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Invasion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Inve$t (Germany) (Compilation - No. 1 Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Inve$t (Germany) (Compilation - World of Business) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Iridon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Iron Lord (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Iron Lord (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Iron Lord (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Iron Tracker (Europe) (Budget - Smash 16) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar 2 - Messengers of Doom (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar 2 - Messengers of Doom (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar 2 - Messengers of Doom (Europe) (En,Fr,De) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar 2 - Messengers of Doom (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar 3 - The Seven Gates of Infinity (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar 3 - The Seven Gates of Infinity (Europe) (En,Fr,De) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar 3 - The Seven Gates of Infinity (France) (ECS) (Equipe Speciale) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar - Legend of the Fortress (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishar - Legend of the Fortress (Europe) (En,Fr,De,It) (Compilation - Ishar Trilogy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ishido - The Way of Stones (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Island of Lost Hope, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ISS - Incredible Shrinking Sphere (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| It Came from the Desert | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| It Came from the Desert (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Italia 1990 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Italian Night 1999 (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Italy 1990 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Italy 1990 (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Italy 1990 - Winners Edition (Europe) (Compilation - Super Sim Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ivan 'Ironman' Stewart's Super Off Road (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ivanhoe (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jack Nicklaus' Course Designers Clip Art - Volume 1 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jack Nicklaus Presents the International Course Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jack Nicklaus Presents The Major Championship Courses of 1991 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jack Nicklaus - The Great Courses of the U.S. Open (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jack Nicklaus - The Major Championship Courses of 1989 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jack Nicklaus' Unlimited Golf & Course Design (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jagd auf Roter Oktober (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jaguar XJ220 (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jahangir Khan World Championship Squash (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Bond 007 - Licence to Kill (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Bond 007 - Live and Let Die (Europe) (Compilation - The James Bond Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Bond 007 - The Spy Who Loved Me (Europe) (Compilation - Superheroes) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Bond 007 - The Spy Who Loved Me (Europe) (Compilation - The James Bond Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Bond - The Stealth Affair (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Clavell's Shogun (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Pond 3 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Pond 3 - Operation Starfish (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Pond II - Codename RoboCod (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Pond II - Codename RoboCod (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Pond II - Codename RoboCod (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Pond - Underwater Agent (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| James Pond - Underwater Agent (Europe) (Compilation - Chart Attack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jaws (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jaws (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jeanne d'Arc (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jet (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jet Set Willy II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jetsons - The Computer Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jetstrike (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jigsaw Mania (Europe) (Note - Mastered with Virus) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jigsaw Puzzlemania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jim Power in Mutant Planet (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jimmy's Fantastic Journey (Europe) (v1.3) (Coverdisk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jimmy White's Whirlwind Snooker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jimmy White's Whirlwind Snooker (Europe) (Compilation - Award Winners - Gold Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jimmy White's Whirlwind Snooker (Europe) (v3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jinks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jinxter (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jinxter (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Joan of Arc (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Joan of Arc - Siege and the Sword (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Joe Blade 2 (Europe) (Budget - Smash 16) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Joe Blade (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Joe & Mac - Caveman Ninja (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| John Barnes European Football (1993)(Krisalis)[!] - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| John Barnes European Football (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| John Lowe's Ultimate Darts (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| John Madden Football (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jonathan (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Journey - The Quest Begins (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Journey to the Centre of the Earth (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jump Jet (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jumping Jack'Son (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jungle Book, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jungle Strike - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jungle Strike (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jungle Strike (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jupiter Probe (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jupiter's Masterdrive (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jurassic Park (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Jurassic Park (Europe) (En,Fr,De,Es,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| K240 (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kalas Puffs Expressen (Sweden) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kamikaze (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kampfgruppe (Europe) (v1.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Karting Grand Prix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Karting Grand Prix + Las Vegas (Europe) (Compilation - Sextett) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Katakis (Europe) (Compilation - Highlights) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Katakis (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kathedrale, Die (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Keef the Thief - A Boy and His Lockpick (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kelly X (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kennedy Approach (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kenny Dalglish's Soccer Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kenny Dalglish - Soccer Match (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Keys to Maramon, The (Europe) (v1.5) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| KGB (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| KGB (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Khalaan (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Khalaan (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 (Europe) (AGA) (Bundled with v1.4e) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 (Europe) (v1.4e) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 (Europe) (v1.6e) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 (Germany) (v1.4g) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 - Giants of Europe (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 (Italy) (v1.4i) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 - Return to Europe (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 (Spain) (v1.4s) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 - The Final Whistle (Europe) (v1.9e) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 - The Final Whistle (Europe) (v1.9g) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 - The Final Whistle (Europe) (v2.1e) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 - Winning Tactics (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 + World Cup '90 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 + World Cup '90 (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 + World Cup '90 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 + World Cup '90 (Germany) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 2 + World Cup '90 (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 3 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 3 (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 3 - European Challenge (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off 3 - European Challenge (Europe) (En,Fr,De,Es,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off (Europe) (En,Fr,De,It,Nl) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off + Extra Time (Europe) (En,Fr,De,It,Nl) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kick Off - Extra Time (Europe) (v2.2) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kid Chaos (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kid Gloves (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kid Gloves II - The Journey Back (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kids' Academy - Which, Where, What (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kikstart 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Killerball (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Killing Cloud, The (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Killing Cloud, The (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Killing Game Show, The (Europe, Australia) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Killing Grounds, The - Alien Breed 3D 2 (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Killing Machine (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King of Chicago, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King of Chicago, The (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest II - Romancing the Throne (Europe) (v2.0J) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest II - Romancing the Throne (Europe) (v2.0J) (Budget - Kixx XL) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest II - Romancing the Throne (Europe) (v2.0J) (HLS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest III - To Heir is Human (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest IV - The Perils of Rosella (Europe) (v1.023) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest - Quest for the Crown (Europe) (v1.000.054) (SCI) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest - Quest for the Crown (Europe) (v1.0U) (HLS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest V - Absence Makes the Heart Go Yonder (Europe) (v1.000.000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest VI - Heir Today, Gone Tomorrow (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| King's Quest VI - Heir Today, Gone Tomorrow (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kingdoms of England II - Vikings - Fields of Conquest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kingdoms of Germany (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kingmaker - The Quest for the Crown (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kingpin - Arcade Sports Bowling (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Klax (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Klax (Europe) (Compilation - Tengen Arcade Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Knight Force (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Knight Orc (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Knightmare (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Knights of the Crystallion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Knights of the Crystallion (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Knights of the Sky (Europe) (v3.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Knights of the Sky (Europe) (v3.04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kristal, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kristal, The (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kristal, The (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Krusty's Fun House (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Krypton Egg (Europe) (Compilation - Big Box) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kult - The Temple of Flying Saucers (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kult - The Temple of Flying Saucers (Europe) (En,Fr,De) (Budget - Bit Star) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Kwasimodo (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| L'Art de la Guerre (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Labyrinth (Denmark) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Labyrinth of Time (RAR) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Labyrinthe aux Mille Calculs, Le (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Labyrinthe d'Anglomania 2, Le (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Labyrinthe d'Errare, Le (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Labyrinthe de la Reine des Ombres, Le (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Labyrinthe de Lexicos, Le (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lamborghini American Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lancaster (Europe) (Compilation - Supreme Challenge - Flight Command) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lancelot (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Las Vegas (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Las Vegas (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Laser Squad (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Laser Squad (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Laser Squad (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Last Action Hero (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Last Battle (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Last Duel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Last Ninja 2 - Back With a Vengeance (Europe) (Compilation - Superheroes) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Last Ninja 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Last Ninja 3 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Last Ninja 3 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leader Board (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leader Board - Tournament Disk 1 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leander (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leander (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leavin' Teramis (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| LED Storm (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Djel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Djel (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Faerghail (Europe) (v2.0e 1990-07-24) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Faerghail (Europe) (v2.0e 1990-10-17) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Faerghail (Germany) (v1.8 1990-06-07) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Faerghail (USA) (v2.0e 1990-10-17) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Kyrandia, The - Book One (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Kyrandia, The - Book One (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of Robin Hood, The - Conquests of the Longbow (Europe) (v1.000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of the Lost (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legend of the Sword (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legends (Europe) (En,Fr,De) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legends of Valour (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legends of Valour (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legends of Valour - Volume 1 - The Dawning (Europe) (Menu - DOS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Legends of Valour - Volume 1 - The Dawning (Europe) (Menu - Quit Game) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leisure Suit Larry 1 - In the Land of the Lounge Lizards (Europe) (v1.000 7.4.1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leisure Suit Larry 5 - Passionate Patti Does a Little Undercover Work (Europe) (v1.000 12.21.91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leisure Suit Larry 5 - Passionate Patti Macht Beim Geheimdienst Mit (Germany) (v1.000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leisure Suit Larry II - Leisure Suit Larry goes Looking for Love (Europe) (v1.003) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leisure Suit Larry III - Passionate Patti in Pursuit of the Pulsating Pectorals (Europe) (v1.039) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leisure Suit Larry in - The Land of the Lounge Lizards (Europe) (v1.05) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings 2 - The Tribes (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings 2 - The Tribes (Europe) (Demo) (Promo - The Future Entertainment Show) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings 2 - The Tribes (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Lemmings (Europe) (Amiga 500 Bundle - Cartoon Classics) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings (Europe) (Book Club) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings (Europe) (Compilation - Award Winners - Platinum Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings (Europe) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lemmings (USA) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leonardo (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Leonardo (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Les Manley in - Search for The King (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lethal Weapon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lethal Xcess - Wings of Death II (Europe) (Amiga + ST) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lethal Zone (Europe) (Coverdisk - Amiga Fun - Issue 10) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lettrix (Germany) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Life & Death (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Light Corridor, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Limes and Napoleon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Line of Fire (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Links Championship Course 1 - Firestone Country Club Akron, Ohio (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Links - The Challenge of Golf (Europe) (v1.53) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Links - The Challenge of Golf (USA) (v1.50) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lion King, The (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lionheart (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Little Computer People (Europe) (Budget - Ricochet 16) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Little Computer People (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Little Puff in Dragonland (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Liverpool - The Computer Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Living Jigsaw (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Log!cal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lollypop (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lombard RAC Rally (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lombard RAC Rally (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lombard RAC Rally (Germany) (Compilation - The Power Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Loom (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Loom (Germany) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Loom (Spain) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Loopz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lord of the Rings Vol. 1 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lord of the Rings Vol. 1 (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lord of the Rings Vol. 1 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lords of Chaos (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lords of Doom (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lords of the Realm (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lords of the Realm (France) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lords of the Rising Sun (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lords of the Rising Sun (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lost Patrol (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lost Patrol (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lost Treasures of Infocom, The (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lost Vikings, The (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Lost Vikings, The (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lost Vikings, The (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lothar Matthaus - Die Interaktive Fussballsimulation (Germany) (En) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lothar Matthaus Super Soccer (Germany) (v2.1 24.10.1994) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lotus Esprit Turbo Challenge (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Lotus Esprit Turbo Challenge (Europe) (Compilation - Chart Attack) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Lotus Esprit Turbo Challenge (Europe) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lotus III - The Ultimate Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lotus Turbo Challenge 2 (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| LotusTrilogyCD32CUEMP3 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lure of the Temptress (Europe) (06.08.1992) (Compilation - The Greatest) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lure of the Temptress (Europe) (17.06.1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lure of the Temptress (Europe) (v2) (6-8-1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lure of the Temptress (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lure of the Temptress (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Lurking Horror, The (USA) (r221) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| M.U.D.S. - Mean Ugly Dirty Sport (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| M.U.D.S. - Mean Ugly Dirty Sport (Spain) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| M1 Tank Platoon (Europe) (v849.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mach 3 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mad News (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mad Professor Mariarti (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mad Show (Europe) (Budget) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mad Show (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mad TV (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magic Boy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magic Fly (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magic Lines (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magic Marble (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magic Pockets (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magician (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magicland Dizzy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Magnetic Scrolls Collection Vol. 1, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Major Motion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Man from the Council, The (Europe) (Compilation - Mega Pack II) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manager, The (Europe) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - Europe (Europe) (En,Fr,De,Es,It) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - Europe (Europe) (En,Fr,De,Es,It) (v2.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - Europe (Europe) (En,Fr,De,Es,It) (v2.3) (Budget - Buzz) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - Premier League Champions (Europe) (En,Fr,De,It) (v1.0 1994-03-11) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - Premier League Champions (Europe) (En,Fr,De,It) (v1.2 1994-03-30) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - The Double (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - The Official Computer Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manchester United - The Official Computer Game (Europe) (Budget - GBH) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manhattan Dealers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manhunter 2 - San Francisco (USA) (v3.06) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manhunter - New York (Europe) (v1.06) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maniac Mansion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maniac Mansion (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maniac Mansion (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maniax (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manic Miner (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manix (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manix (Europe) (En,Fr,De) (Budget - GBH) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Manoir de Mortvielle, Le (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Marble Madness (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Marvin's Marvellous Adventure (Europe) (En,Fr,De,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Masterblazer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Match of the Day (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Match Pairs (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Matrix (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Matrix Marauders (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maupiti Island (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maupiti Island (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maupiti Island (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Maya (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mayday Squad Heroes (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| McDonaldland (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mean 18 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mean Arenas - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Medieval Warriors (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mega lo Mania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mega lo Mania (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mega lo Mania (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mega Motion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mega Phoenix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mega Twins (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Megafortress (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MegaTraveller 1 - The Zhodani Conspiracy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MegaTraveller 2 - Quest for the Ancients (Europe) (v1.05) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MegaTraveller 2 - Quest for the Ancients (Germany) (v1.04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Menace (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mercenary - Escape from Targ + The Second City (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mercenary III - The Dion Crisis (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Merchant Colony (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mercs (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Metal Masters (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Miami Chase (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mickey 123 - L'Anniversaire Surprise (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mickey ABC - Une Journee a la Fete (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mickey et la Machine a Mots Croises (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mickey Jeu de Memoire (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mickey Puzzles Animes (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mickey's Runaway Zoo (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Micro Machines (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Microdeal Hit Disks - Volume 1 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MicroProse Collection for 1991-92 (Europe) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MicroProse Formula One Grand Prix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MicroProse Golf (Europe) (v1.3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MicroProse Soccer (Europe) (Compilation - Soccer Stars) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Midnight Resistance (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Midwinter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Midwinter (Europe) (Compilation - Virtual Reality - Vol. 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Midwinter (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Midwinter (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Midwinter II - Flames of Freedom (Europe) (1MB, v1 1991-09-07) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Midwinter II - Flames of Freedom (Germany) (v1 1991-09-07) (1MB) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MiG-29 Fulcrum (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MiG-29 Soviet Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MiG-29M Super Fulcrum (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Might and Magic II - Gates to Another World (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Might and Magic III - Isles of Terra (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mighty BombJack (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mike Read's Computer Pop Quiz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mike the Magic Dragon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Milky Way Cafe (Europe) (v0.2 8.1.92) (Beta) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Milky Way Cafe (Europe) (v1.2 8.1.92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Millenium - Return to Earth (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Millennium 2.2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Millennium 2.2 (Europe) (Budget - Bit Star) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mind Forever Voyaging, A (USA) (r79) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mind Walker (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mindbender (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mindfighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mindroll (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mindshadow (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mini Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Minos (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Minskies... the abduction (Europe) (ECS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Missiles Over Xerion + City Defence (Europe) (Bonus Game) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mission Elevator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mixed-Up Mother Goose (Europe) (v1.000) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moebius - The Orb of Celestial Harmony (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monkey Business (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monkey Island 2 - LeChuck's Revenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monkey Island 2 - LeChuck's Revenge (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monkey Island 2 - LeChuck's Revenge (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monopoly (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monster Business (Europe) (Amiga + ST) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monty Python's Flying Circus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Monty Python's Flying Circus (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moochies, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moon Blaster (France) (Compilation - Top 3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moonbase - Lunar Colony Simulator (Europe) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moonfall (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moonmist (USA) (r4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| MoonShine Racers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moonstone - A Hard Days Knight (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Moonwalker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Morph - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Morph (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Morph (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mortal Kombat (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Mortal Kombat (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mortal Kombat II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mortville Manor (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Motoerhead (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Motor Massacre (Europe) (Compilation - Action Amiga) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Motorbike Madness (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mouse Trap (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Movem + 3D-Motorrad (Germany) (Coverdisk - Amiga Mania - Issue June '92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mr Blobby (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mr Do! Run Run (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mr. Heli (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Mr. Nutz - Hoppin' Mad (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Multi-Player Soccer Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Munsters, The (Europe) (Compilation - Kids Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Murder! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Murder! (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Murders in Space (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Murders in Venice (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| My Funny Maze (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mysterious Worlds (Europe) (Coverdisk - Amiga Fun - Issue 02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mystery of the Mummy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Mystical (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Myth (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Myth - History in the Making (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 'Nam 1965-1975 (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Napoleon I - The Campaigns 1805-1814 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Napoleonics (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| NARC (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nathan Never (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Naughty Ones (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Naughty Ones (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Navy Moves (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Navy Moves (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Navy SEALs (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nebulus 2 - Pogo a Gogo (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Nebulus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nebulus (Europe) (Budget - Black Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Necronom (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Neighbours (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Netherworld (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Netherworld (Europe) (Compilation - Premier Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Neuromancer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Neuronics (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Never Mind (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| NeverEnding Story II, The - The Arcade Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| New York Warriors (Europe) (512KB) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| New York Warriors (USA) (1MB) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| NewZealand Story, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| NewZealand Story, The (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nick Faldo's Championship Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nicky Boom (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Nigel Mansell's Grand Prix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nigel Mansell's World Championship (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nigel Mansell's World Championship (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Night Hunter (Europe) (Compilation - 10 Great Games) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Night Shift (Europe) (Compilation - Maximum Action Xtra) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Night Walk (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nightbreed - The Action Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nightbreed - The Action Game (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nightbreed - The Interactive Movie (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nightdawn (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nightdawn (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| NightHawk F-117A Stealth Fighter 2.0 (Europe) (v3.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ninja Mission (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ninja Rabbits (Europe) (Compilation - Mega Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ninja Remix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ninja Spirit (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Ninja Warriors, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nippon Safes Inc. (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nitro Boost Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nitro (Europe, Australia) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| No Buddies Land (Europe) (Unl) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| No Excuses (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| No Exit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| No Greater Glory - The American Civil War (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| No Second Prize (Europe) (7 Saves Left) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| No Second Prize (Europe) (8 Saves Left) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nobby The Aardvark (Europe) (En,Fr,De) (Proto) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Noddy's Playtime (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nord and Bert Couldn't Make Head or Tail of It (USA) (r19) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| North Sea Inferno, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| North & South (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| North & South (Europe) (En,Fr,De,Es,It) (Compilation - Le Temps des Heros) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| North & South (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nova 9 - The Return of Gir Draxon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nuclear War (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Nucleus (Europe) (Compilation - Bug Bash + Nucleus) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Obitus (Europe, Australia) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Obliterator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Obsession (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Odyssey (Europe) (aud304312a 1995-05-23) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Odyssey (Europe) (aud304312b 02.01.96) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Odyssey (Europe) (aud304312c 02.06.97) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Off Shore Warrior (Europe) (Compilation - Titus Action II) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Official Everton F.C. Intelligensia, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ogre (USA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Oh No! More Lemmings (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oh No! More Lemmings (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oh No! More Lemmings (USA) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oil Imperium (Europe) (v3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oil Imperium (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oldtimer - Erlebte Geschichte Teil II (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Olympique de Marseille (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Omega (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Omni-Play Basketball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Omnicron Conspiracy (Europe) (v1.0 191090) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Omnicron Conspiracy (France) (v1.0 191090) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Omnicron Conspiracy (Germany) (v1.0 191090) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Omnicron Conspiracy (Germany) (v1.0 191090) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| On the Ball - League Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| On the Ball - League Edition (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| On the Ball - World Cup Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| On the Ball - World Cup Edition (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| One-on-One (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| One Step Beyond (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Onescapee (1997) (Sadeness Softwares) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Onslaught (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ooops Up (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ooze - Als die Geister muerbe wurden (Germany) (v2.0D) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ooze - Creepy Nites (Europe) (v1.2E) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Combat II - By Land, Sea and Air (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation GII (Europe) (Demo) (Coverdisk - Amiga Format - Issue 63) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Harrier (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Jupiter (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Neptune (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Stealth (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Stealth (Europe) (Compilation - The Delphine Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Stealth (France) (Compilation - Les Maitres de l'Aventure) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Stealth (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Thunderbolt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Wolf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Operation Wolf (Europe) (Compilation - Magnum 4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Orbital Destroyer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oriental Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oriental Games (Europe) (Compilation - Magnum) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ork (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ork (Europe) (Demo, Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ork (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oscar (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oscar (Europe) (AGA) (Amiga 1200 Bundle - Desktop Dynamite) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oswald (Denmark) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Othello Killer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Out of This World (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Out To Lunch - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| OutRun Europa (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| OutRun (Europe) (1987) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| OutRun (Europe) (1987) (Compilation - Giants) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| OutRun (USA) (1988) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Outzone (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Over the Net (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Overdrive (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Overkill (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Overkill & Lunar-C (1993)(Mindscape)[!] (CD32) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Overlander (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Overlord (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oxford Softworks, The - Go Player (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Oxxonian (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| P.O.W. (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| P.P. Hammer and His Pneumatic Weapon (1991)(Demonware)[cr CSL] | TBD | Operational (visual smoke) | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| P.P. Hammer and His Pneumatic Weapon (1991)(Demonware)[cr CSL][h PRX] | TBD | Visual defect | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| P.P. Hammer and His Pneumatic Weapon (1992)(Global Software)[budget] | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| P.P. Hammer and His Pneumatic Weapon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| P.P. Hammer and His Pneumatic Weapon (Europe) (Budget - Global Software) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| P.P. Hammer and His Pneumatic Weapon (PPHammer.lha) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| P47 Thunderbolt (1990)(Firebird)[cr MAD] | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| P47 Thunderbolt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| P47 Thunderbolt (Europe) (Compilation - Air-Sea Supremacy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pac-Land (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pac-Mania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pacific Islands (Europe) (En,Fr,De,Es) (Compilation - Combat Classics 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paladin (Europe) (v1.0e) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paladin II (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pandora (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pang-500-NTSC | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pang-500-NTSC.adf | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Pang-AGA | TBD | Requirement/error prompt | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| Pang (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Panza Kick Boxing (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Panza Kick Boxing (France) (Compilation - Podium) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paperboy 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paperboy (Europe) (Mastering Errors) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paradroid 90 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paradroid 90 (Europe) (Compilation - Amiga Challenge) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paragliding Simulation (Europe) (En,Fr,De,Es) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paramax (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paranoia Complex, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Paranoia Complex, The (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Parasol Stars - Rainbow Islands 2 (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Parasol Stars - Rainbow Islands 2 (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Passing Shot (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Patrician, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Patrizier, Der (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pawn, The (Europe) (v2.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pegasus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Penthouse Hot Numbers Deluxe (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Perfect General, The (Europe) (v1.02 11.19.1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Perfect General, The (France) (v1.02 11.19.1991) (Compilation - Battles of Time) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Perfect General, The (Germany) (v1.02 11.19.1991) (Compilation - The Lords of Power) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Perfect General, The - Scenario Disk - World War II Battle Set (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Perihelion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Persecutors (Germany) (En) (Compilation - The Power Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Persian Gulf Inferno, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Personal Nightmare (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Peter Beardsley's International Football (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Peter Pan (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PGA European Tour (1994)(Ocean) (CD32) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PGA European Tour (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PGA European Tour (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PGA Tour Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PGA Tour Golf - Tournament Course Disk (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phalanx (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phalanx II - The Return (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phantasie (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phantasie III - The Wrath of Nikademus (Europe) (v1.0) (Compilation - Phantasie - Bonus Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phantasm (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phantasm (Europe) (Budget) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phantom Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Phobia (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Photon Storm (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pick'n Pile (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pick'n Pile (Europe) (Compilation - Le 2eme Sens) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pictionary (Europe) (v3.1a) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pierre le Chef Is... Out to Lunch (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinball Dreams | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Pinball Dreams (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Pinball Dreams NTSC | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Pinball Fantasies (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Pinball Fantasies (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinball Fantasies (USA) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinball Illusions (Europe) (AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Pinball Magic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinball Magic (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinball Mania (Europe) (AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Pinball Mania (Europe) (AGA) (Amiga 1200 Bundle - Amiga Magic) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Pinball Prelude (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinball Prelude (Europe) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinball Wizard (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PinballFantasiesCD32CUE | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pink Panther (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pinkie (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pioneer Plague (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pipe Mania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Piracy on the High Seas (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pirates | TBD | Visual defect | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Pirates! (Europe) (v832.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pirates! (Europe) (v832.04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pit-Fighter (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Pixie & Dixie featuring Mr Jinks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pizza Connection (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Plan 9 from Outer Space (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Planetfall (Europe) (r37) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Platoon (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Play Disk | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Playdays (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Player Manager 2 (Europe) (v28.7.1995) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Player Manager 2 Extra - The Chase for Glory (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Player Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Player Manager (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Player Manager (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Playroom, The - La Chambre de Peppy (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Plotting (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Plutos (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Plutos (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Poker Nights - Teresa Personally (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Police Quest 2 - The Vengeance (Europe) (v1.024) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Police Quest 3 - The Kindred (USA) (v1.000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Police Quest - In Pursuit of the Death Angel (Europe) (v2.0B 2.22.89) (Budget - Kixx XL) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Police Quest - In Pursuit of the Death Angel (USA) (v2.0B 2.22.89) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pool (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Populous (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Populous (Europe) (v2.7) (1989-03-17) (Budget) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Populous II - The Challenge Games (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Populous II - Trials of the Olympian Gods (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Populous - The Promised Lands (Europe) (Addon) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Populous - The Promised Lands (Europe) (Amiga + ST) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Populous (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Populous World Editor (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Portal (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ports of Call (Europe) (v1.1) (Amiga 600 Bundle - Smart Start) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Postman Pat 3 - To the Rescue! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Postman Pat (Europe) (Compilation - Kids Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pot Panic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Power Drift (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Power Drift (Europe) (Compilation - Wheels of Fire - The Ultimate Driving Compilation) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Power Drive (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Power Struggle (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Power, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PowerBoat USA (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Powerdrome (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PowerMonger | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PowerMonger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PowerMonger (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PowerMonger - WW1 Edition (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Powerplay - The Game of the Gods (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Powerplay - The Game of the Gods (Europe) (Compilation - Astra Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Powerstyx (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Powerstyx (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Predator 2 (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prehistoric Tale, A (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Prehistorik (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Preis ist heiss, Der (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Premier Manager 2 - The New Season (Europe) (En,De) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Premier Manager 3 Deluxe (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Premier Manager 3 (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Premier Manager 3 (Europe) (AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Premier Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Premier Manager Multi-Edit System (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Premiere - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Premiere (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| President is Missing, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| PREY An Alien Encounter (1993) (Almathera) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Primal Rage (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prime Mover (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prince (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prince of Persia | TBD | Operational (visual smoke) | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| Prince of Persia (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prince of Persia (France) (Compilation - Super Heros) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prince of Persia (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prince of Persia (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prison (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pro Boxing Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pro PowerBoat Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pro PowerBoat Simulator + Nitro Boost Challenge (Europe) (Compilation - Quattro Power Machines) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pro Tennis Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pro Tennis Tour 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pro Tennis Tour (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Pro Tennis Tour (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Produzent, Der - Die Welt des Films (Germany) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Professional Football Simulation (Europe) (v3.1b) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Profezia (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Profi-Fussball - Der Trainer (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProFlight (Europe) (v1.51) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Project One (Europe) (Coverdisk - Amiga Fun) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Project-X (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Project-X (Europe) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Project X F17 Challenge - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Project-X - Special Edition '93 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Projectyle (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Projekt Prometheus (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prophecy I - The Viking Child (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ProSoccer 2190 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Prospector - In the Mazes of Xor + StarRay (Europe) (Demo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Protector (Europe) (Paradox) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Protector (Europe) (VM) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Psyborg (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Psygnosis Demos CDTV (RAR) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Pub Trivia Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Puffy's Saga (Europe) (Compilation - Winning 5) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Puggsy (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Punica Spiel, Das (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Purple Saturn Day (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Purple Saturn Day (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Push-Over (Europe) (En,Fr,De,Es) (Copyright Ocean Title) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Push-Over (Europe) (En,Fr,De,Es) (Copyright Red Rat & Ocean Title) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Putty (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Puzznic (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Pyramax (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| QBall (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| QIX (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quadralien (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quadralien (Europe) (En,Fr,De) (With StarRay Demo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quadrel (Europe) (En,Fr) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quadrel (Europe) (En,Fr) (v1.0) (Q.I. 1992) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quantox (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quartz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quest for Glory II - Trial by Fire (Europe) (v1.109) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quest for the Time-Bird, The (Europe) (En,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quest of Agravain (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Question of Sport, A (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Questron II (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quete de l'Oiseau du Temps, La (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quik - The Thunder Rabbit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Quik The Thunder Rabbit (RAR) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Quintette (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Qwak (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| R.B.I. Two Baseball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| R.B.I. Two Baseball (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| R-C Aerochopper (USA) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| R-Type | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| R-Type (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| R-Type II (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Raffles (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Raider (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Railroad Tycoon (Europe) (v855.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Railroad Tycoon (Europe) (v855.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Railroad Tycoon (Germany) (v855.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Railroad Tycoon (Germany) (v855.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rainbow Islands | TBD | Visual defect | TBD | TBD | TBD | Visual defect | Visual defect | TBD | TBD | TBD |
| Rainbow Islands (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rainbow Islands (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rainbow Islands (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rally Championships (Europe) (En,Fr,De,Es,It,Nl,Pt,Pl) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rally Championships (Europe) (En,Fr,De,Es,It,Nl,Pt,Pl) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rally Cross Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rambo III (Europe) (Budget - The Hit Squad) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rambo III (USA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rampage (USA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rampart (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| RanX (France) (Compilation - 10 Megahits Vol. 3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reach for the Skies (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reach for the Stars - The Conquest of the Galaxy (Europe) (v3.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reach for the Stars - The Conquest of the Galaxy (USA) (v3.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Real Ghostbusters, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Realm of the Trolls (Europe) (Compilation - 5th Anniversary) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Realms (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Realms of Arkania - Blade of Destiny (Europe) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Recognize Me (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Red Baron (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Red Baron (Europe) (Compilation - The Lords of Power) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Red Baron (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Red Heat (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Red Heat (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Red Lightning (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Red Storm Rising (Europe) (v843.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reeder, Der (Germany) (v1.00 24.08.1995) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reeder, Der (Germany) (v1.12 - 1995-08-24) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reederei (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reel | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reel_1.adf | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reel_2.adf | TBD | Prompt only | TBD | TBD | TBD | Prompt only | Prompt only | TBD | TBD | TBD |
| Reise zum Mittelpunkt der Erde (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Renaissance (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Renegade (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Renegade (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Renegade Legion - Interceptor (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Resolution 101 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Retee! (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Return to Atlantis (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reunion (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Reunion (Germany) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Revelation! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rick Dangerous 2 (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rick Dangerous 2 (Europe) (Budget - Kixx) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rick Dangerous (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rick Dangerous (Europe) (Amiga + PC) (Budget - Kixx) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rick Dangerous (Europe) (Level Select) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rick Dangerous (Europe) (Unlimited Lives) (Alt 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rick Davis's World Trophy Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ringling Bros. and Barnum & Bailey Circus Games (Europe) (Compilation - Mega Pack II) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rings of Medusa (Europe) (v1.6) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rings of Medusa (Germany) (v1.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rings of Medusa (Germany) (v1.04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rings of Medusa (Germany) (v1.6) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rings of Medusa II - The Return of Medusa (Germany) (v1.07C+ AUG 28 1991) (Coverdisk - Amiga Fun - Issue xx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ringside (Europe) (Compilation - Hyperaction) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rise of the Dragon (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rise of the Dragon (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rise Of The Robots - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rise of the Robots (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rise of the Robots (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Risk - The World Conquest Game (Europe) (v1.9) (Compilation - Board Genius) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Riskant! (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Risky Woods (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Road Blasters (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Road Blasters (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Road Rash (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Roadkill (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Roadwar 2000 | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Roadwar Europa (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Roadwars (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Robbeary (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Robin Smith's International Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Robinson's Requiem (Europe) (En,Fr,De) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| RoboCop 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| RoboCop 3 (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| RoboCop 3 (Europe) (En,Fr,De) (Dongle Protected) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| RoboCop 3D (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| RoboCop (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| RoboCop (Europe) (Budget - The Hit Squad) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| RoboCop (Europe) (Compilation - Hollywood Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| RoboSport (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Robotnic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Robozone (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rock-A-Doodle - The Computerized Coloring Book (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rock 'n Roll (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rock 'n Roll (Europe) (Compilation - 5th Anniversary) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rock Star Ate My Hamster (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rocket Ranger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rocket Ranger (Europe) (Budget - Mirror Image) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rocket Ranger (Germany) (En) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rocket Ranger (Germany) (En) (Budget - Mirror Image) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rocky (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rodland (Europe) (v1.3) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rodland (Europe) (v1.32) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Rody and Mastico (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rody et Mastico (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rody et Mastico II (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rody et Mastico III (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rody et Mastico IV - Rody Noel (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rody et Mastico V (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rody et Mastico VI (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rody und Mastico (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rogue Trooper (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Roller Coaster Rumbler (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rollerball (Europe) (Coverdisk - Amiga Special - Issue xx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rolling Ronny (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rolling Ronny (Europe) (Coverdisk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rolling Thunder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rolling Thunder (Europe) (Budget - KlassiX) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Romantic Encounters at the Dome (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rome AD 92 - The Pathway to Power (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rotor (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rotox (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Round the Bend! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rubicon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ruesselsheim (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ruff and Reddy in the Space Adventure (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ruff 'n' Tumble (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Ruffian (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rugby Coach (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rugby League Coach (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rugby - The World Cup (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rugby - The World Cup (Europe) (With Demo Option) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rules of Engagement (Europe) (v1.06 11.26.91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Rules of Engagement (USA) (v1.03 10.14.91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Run the Gauntlet (Europe) (Compilation - Party Time) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Run the Gauntlet (Europe) (Compilation - Sports Collection) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Running Man, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| RVF Honda (Europe) (Compilation - Magnum) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ryder Cup - Johnnie Walker (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| S.D.I. (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| S.D.I. (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| S.T.U.N. Runner (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sabre Team (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sabre Team (Europe) (En,Fr,De,Es,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Saddam Hussein Game, The (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Saint and Greavsie (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Samurai - The Way of the Warrior (Europe) (En,Fr,De,Es,It) (v1.01) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Santa's Xmas Caper (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sarakon (Europe) (Starbyte) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sarakon (Europe) (Virgin) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sarcophaser (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sargon III (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SAS Combat Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SAS Combat Simulator + MiG-29 Soviet Fighter + Kamikaze (Europe) (Compilation - Quattro Fighters) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Satan (Europe) (Compilation - Magnum) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Savage (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Scary Mutant Space Aliens from Mars (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Schwarze Auge, Das - Die Schicksalsklinge (Germany) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sci-Fi (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Scooby-Doo and Scrappy-Doo (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Scorpio (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Scorpion (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Scrabble Deluxe (Europe) (Compilation - Board Genius) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Scrabble Deluxe (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Scramble Spirits (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Screaming Wings (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SDI (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| SeaHaven Towers (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Seastalker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Second Samurai (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Second Samurai (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Seconds Out (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Secret of Monkey Island, The (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Secret of Monkey Island, The (France) (v1.0 4-26-91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Secret of Monkey Island, The (Germany) (v1.2r 1991-03-26) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Secret of Monkey Island, The (Italy) (v1.0 07-2-91) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Seek & Destroy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible Soccer (Europe) (En,Fr,De,It) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible Soccer - European Champions (Europe) (En,Fr,De,It) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible Soccer - European Champions (Europe) (En,Fr,De,It) (v1.1) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible Soccer - European Champions (Europe) (En,Fr,De,It) (v1.1) (Compilation - Award Winners - Gold Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible Soccer - International Edition (Europe) (En,Fr,De,It) (v1.2) (Compilation - Help!) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible Soccer - International Edition - World Champions (Europe) (En,Fr,De,It) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible World of Soccer '95-'96 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible World of Soccer '95-'96 - European Championship Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible World of Soccer '96-'97 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible World of Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible World of Soccer (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sensible World of Soccer - Update Disk (Europe) (v1.1) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sentinel, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Settlers, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Settlers, The (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Seven Cities of Gold (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Seven Gates of Jambala, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Seven Gates of Jambala, The (Europe) (Compilation - The First Year) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sex Vixens from Space (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Seymour Goes to Hollywood (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow Dancer (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Shadow Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow Fighter (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow of the Beast + Blood Money (Europe) (Demo, Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow of the Beast (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Shadow of the Beast (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow of the Beast II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow of the Beast II (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow of the Beast III (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow of the Beast III (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow Warriors (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadow Warriors (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadowgate (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadowlands (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shadoworlds (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shanghai '98 (Germany) (Coverdisk - Amiga Special - Issue xx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shanghai (Europe) (Compilation - Power Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shanghai (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shaq-Fu (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shaq-Fu (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sharkey's Moll (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| She Fox (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sherman M4 (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shiftrix (Europe) (23.4.1991) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shinobi (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shinobi (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ShockWave (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shoot'Em-Up Construction Kit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shoot'Em-Up Construction Kit (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shooting Star (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shortgrey, The (France) (Note - Mastered with virus) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shuffle (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shufflepuck Cafe (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shufflepuck Cafe (France) (v1.0) (Compilation - NRJ - La Compil'Action Vol.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shuttle - The Space Flight Simulator (Europe) (En,Fr,De) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Shuttle - The Space Flight Simulator (Europe) (En,Fr,De) (v1.0) (No Title Screen) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Side Arms (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SideShow (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SideWinder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SideWinder II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Siedler, Die (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sierra Soccer - World Challenge Edition (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Silent Service II (USA) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Silent Service - The Submarine Simulation (Europe) (v825.03) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Silicon Dreams (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Silkworm (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Silkworm (Europe) (Alt) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Silly Putty (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimAnt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimAnt (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimAnt (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity 2000 (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity 2000 (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity - Architecture 1 - Future Cities (USA) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity - Architecture 2 - Ancient Cities (USA) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity (Europe) (512KB) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| SimCity (Europe) (512KB) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity (Europe) (512KB) (Alt 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity (Europe) (v1.2) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| SimCity - Terrain Editor (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity (USA) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimCity (USA) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimEarth - The Living Planet (Europe) (v1.0 1992-07-08) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimEarth - The Living Planet (France) (v1.0 1992-07-08) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimLife (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| SimLife (Europe) (AGA) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| SimLife (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimLife (France) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimLife (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SimLife (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simon the Sorcerer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simon the Sorcerer (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simon the Sorcerer (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simon the Sorcerer (France) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simon the Sorcerer (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simon the Sorcerer (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simpsons, The - Bart vs. The Space Mutants (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simpsons, The - Bart vs. The Space Mutants (Europe) (Amiga 500 Plus Bundle - Cartoon Classics) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simpsons, The - Bart vs. The World (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Simulcra (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sinbad and the Throne of the Falcon (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sink or Swim (Europe) (OCS, ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sir Fred (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sixiang (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sixth Sense Investigations (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skaermtrolden Hugo (Denmark) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skate of the Art (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skeet Shoot (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skeleton Krew (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ski or Die (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skidmarks (Europe) (v1.06) (OCS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skidmarks Racer Issue 1 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skidz (Europe) (Compilation - 16 Bit Hit Machine) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skrull the Barbarian (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skull & Crossbones (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Skweek (Europe) (Compilation - Les Stars) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sky Fighter (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sky High Stuntman (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skyblaster (Europe) (Compilation - Amiga Star Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SkyChase (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SkyChase (Europe) (Compilation - Supreme Challenge - Flight Command) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skyfox II - The Cygnus Conflict (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skyfox (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Skyfox (USA) (HLS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Slamtilt (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Slayer (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Slayer (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Slaygon (USA) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sleeping Gods Lie (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sleepwalker (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Sleepwalker (Europe) (A1200 Bundle - Comic Relief Pack) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Sleepwalker (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sleepwalker (Europe) (AGA) (A1200 Bundle - Comic Relief Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sliders (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Slightly Magic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SlipStream (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sly Spy Secret Agent (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Smash T.V. (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Snapperazzi - The Alien Who Invades Your Space! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Snoopy - The Cool Computer Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Snoopy - The Cool Computer Game (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Snow Bros. (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Snow Strike (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soccer Kid (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soccer Kid (Europe) (En,Fr,De,Es,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soccer King (Germany) (Compilation - Shooting Stars 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soccer Pinball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soccer Star - World Cup Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soccer Team Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Software Manager (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soldier 2000 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soldier of Light (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soldner (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Solitaire's Journey (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Son Shu-Shi (Europe) (En,Fr) (Unl) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sonic Boom (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sooty and Sweep (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sophelie (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sorcerer Lord (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sorcery Plus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Soul Crystal (Germany) (v1.5) (Budget - Top Shots) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space 1889 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Ace (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Ace II - Borf's Revenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Assault (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Battle (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Crusade (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Crusade - The Voyage Beyond (Europe) (En,Fr,De,It) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Fight (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Gun (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Harrier (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Harrier II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Harrier + James Bond 007 - Live and Let Die (Europe) (The Story so Far Vol. 3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Harrier + Space Harrier - Return to the Fantasy Zone (Europe) (Compilation - Finale) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Hulk (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Job (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Quest 1 - Roger Wilco In The Sarien Encounter (Europe) (v1.000) (Enhanced) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Quest Chapter 1 - The Sarien Encounter (USA) (v1.2) (HLS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Quest II - Vohaul's Revenge (Europe) (v2.0f) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Quest III - Die Piraten von Pestulon (Germany) (v1.000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Quest III - The Pirates of Pestulon (Europe) (v1.0V 1989-08-17) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Quest IV - Roger Wilco and the Time Rippers (USA) (v1.000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Quest IV - Roger Wilco und die Zeitspringer (Germany) (v1.000) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Racer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Racer (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Ranger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Rogue (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Station (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Space Station Oblivion (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spaceball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SpaceCutter (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spaceport (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spaceward Ho! (Germany) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spacewrecked - 14 Billion Light Years From Earth (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Special Forces (Europe) (En,Fr,De) (v1 1992-02-10) (1MB) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Speedball 2 - Brutal Deluxe (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Speedball 2 - Brutal Deluxe (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Speedball 2 - Brutal Deluxe (Europe) (v1.00) (Compilation - The Bitmap Brothers - Volume 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Speedball 2 - Brutal Deluxe (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Speedball (Europe) (v1.10) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Speedball (USA) (v1.05) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Speedboat Assassins (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spell Book 7 Plus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spellbound Dizzy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spellbound (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spellfire the Sorcerer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Speris Legacy, The (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spherical (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spherical Worlds (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spidertronic (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spike in Transylvania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spindizzy Worlds (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spinworld (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spirit of Adventure (Germany) (Coverdisk - Amiga Fun - Issue xx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spirit of Excalibur (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spirit of Excalibur (Europe) (Compilation - Strategy Masters) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spirit of Excalibur (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spitting Image (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spot - The Computer Game! (Europe) (v1.6) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spy vs Spy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spy vs Spy II - The Island Caper (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Spy vs Spy III - Arctic Antics (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| St Dragon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stack Up (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stalingrad (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Breaker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Command (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Control (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Crusader (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Fleet I - The War Begins! (Europe) (v2.1 12.11.86) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Trash (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Trek - 25th Anniversary (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Trek - 25th Anniversary (France) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Trek - 25th Anniversary (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Wars (Europe) (Atari) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Wars (Europe) (Tengen) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Wars + James Bond 007 - Licence to Kill (Europe) (Compilation - Heroes) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Wars - Return of the Jedi (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Wars - Return of the Jedi (Europe) (Compilation - The Star Wars Trilogy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Wars - The Empire Strikes Back (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Star Wars - The Empire Strikes Back (Europe) (Compilation - The Star Wars Trilogy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starblade (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starblaze (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starcross (USA) (r17) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stardust (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stardust (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| StarFlight (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| StarFlight II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starglider 2 (Europe) (En,Fr,De) (Amiga + ST) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| StarGlider (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| StarGoose! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starlord (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starlord (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| StarRay (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| StarRay (Europe) (Budget - The 16 Bit Pocket Power Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| StarRay (Europe) (Compilation - Hyperaction) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starush (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Starways (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stationfall (USA) (r107) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Steel Empire (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Steel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Steel (Europe) (Budget - Black Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Steg the Slug (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Steigar (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stellar 7 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stellar 7 (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stellar Crusade (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Steve Davis World Snooker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stone Age (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stoppt den Calippo Fresser (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Storm Across Europe - The War in Europe - 1939-45 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Storm Master (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Storm Master (France) (Compilation - Magic Worlds) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stormball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stormlord (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strange New World (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Street Fighter (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Street Fighter (Europe) (Amiga + PC) (Budget - Kixx) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Street Fighter II - The World Warrior (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Street Fighter II - The World Warrior (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Street Racer (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Street Sports Basketball (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strider (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strider II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strider II + Indiana Jones and the Last Crusade - The Action Game (Europe) (Compilation - Superheroes) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strike Aces (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strike Force Harrier (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strike Force Harrier (Europe) (Budget - Mirror Image) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strikefleet (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Striker (Europe) (Compilation - Amiga Zool Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Striker Manager (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Strip Poker II+ + Karting Grand Prix (Demo) + XR35 (Demo) (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stryx (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stundenglas, Das (Germany) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stunt Car Racer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Stunt Track Racer (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sub Battle Simulator (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Subbuteo - The Computer Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Subtrade - Return to Irata (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Suburban Commando (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Suburban Commando (Europe) (Compilation - The Sci-Fi Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SubVersion 1.0 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Subwar 2050 (Europe) (En,Fr,De,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Suicide Mission (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sumera (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sumera + Takado (Germany) (Compilation - Powerbox) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Summer Camp (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Summer Games (Europe) (Compilation - Mega Sports) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Summer Games II (Europe) (Compilation - Mega Sports) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Summer Olympiad (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sun Crosswords, The - Volume 1 & 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Supaplex (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Cars (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Cars (Europe) (Budget - GBH) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Cars (Europe) (Compilation - 16 Bit Hit Machine) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Cars II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Cauldron + Crazy Cars 3 (Europe) (Demo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Hang-On (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Huey - UH-1X (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super League Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super League Manager (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Monaco GP (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Scramble Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Seymour Saves the Planet (Europe) (23.7.92) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Ski (Europe) (Amiga 500 Bundle - Starter Kit) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Ski II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Skidmarks (Europe) (v2.2) (OCS, AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Skweek (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Skweek (France) (Compilation - Les Stars) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Space Invaders (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Space Invaders (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Sport Challenge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Stardust (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Street Fighter II - The New Challengers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Street Fighter II - The New Challengers (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Street Fighter II Turbo (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Tennis Champs (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Tetris (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Wonderboy - Wonderboy in Monsterland (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Super Zocker + Blackjack II (Germany) (Coverdisk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Superfrog (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Superfrog (LHA) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Superleague Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Superman - The Man of Steel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Superstar Ice Hockey (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Supremacy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Surf Ninjas (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Suspect (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Suspended (USA) (r8) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Suspicious Cargo - Special Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Swap (Europe) (v2.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Swap (France) (Compilation - NRJ - La Compil'Action Vol.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Switchblade (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Switchblade II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SWIV (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Swooper (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Swooper + Space Baller + Diablo + Zitrax + Othello (Europe) (Compilation - Amiga Stars Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sword and the Rose, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sword of Aragon (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sword of Honour (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Sword of Sodan (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Swords of Twilight | TBD | Operational (visual smoke) | TBD | TBD | TBD | Operational (visual smoke) | Operational (visual smoke) | TBD | TBD | TBD |
| Swords of Twilight (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Swords of Twilight (LHA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Syndicate | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Syndicate (1) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Syndicate (Europe) (En,Fr,It) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Syndicate (Europe) (En,Fr,It) (Compilation - Chaos Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Syndicate (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Syndicate (LHA) | TBD | Unsupported archive entries | TBD | TBD | TBD | Unsupported archive entries | Unsupported archive entries | TBD | TBD | TBD |
| Table Tennis Simulation (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tactical Manager 2 (Europe) (v2.43) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tactical Manager 94-95 Season (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tactical Manager (Europe) (v2.36) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tactical Manager - Italia (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Takado (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Take-em-Out (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tanglewood (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tangram (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tank Attack (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Targhan (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Targhan (France) (Compilation - Simulation Top) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tass Times in Tonetown (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Team Suzuki (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Team Yankee (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tearaway Thomas (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tecnoball Z (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tee Off! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Teenage Mutant Hero Turtles (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Teenage Mutant Hero Turtles - The Coin-Op! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Teenage Mutant Ninja Turtles (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Teenage Queen (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Telekommando!, Das (Germany) (v1.0) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tennis Cup (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tennis Cup (France) (Compilation - NRJ - La Compil'Action Vol.4) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tennis Cup II (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Terminator 2 - Judgment Day (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Terminator 2 - Judgment Day (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Terminator 2 - The Arcade Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Terramex (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Terran Envoy (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Terrorpods (Europe) (En,Fr,De,Sv,No,Da) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Terry's Big Adventure (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Drive (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Drive (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Drive II Car Disk - The Muscle Cars (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Drive II Car Disk - The Supercars (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Drive II Scenery Disk - California Challenge (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Drive II Scenery Disk - European Challenge (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Drive II - The Duel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Test Match Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Testament (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tetra Quest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tetris (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tetris + Joe Blade (Europe) (Compilation - Computer Hits - Volume Two) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thai Boxing (Europe) (Anco) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thai Boxing (Europe) (Budget) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Theatre of Death (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Their Finest Hour - The Battle of Britain (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Their Finest Hour - The Battle of Britain (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Their Finest Missions - Volume 1 (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Theme Park (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Theme Park (Europe) (En,Fr,De,It) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Theme Park Mystery (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Theme Park Mystery (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thexder (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Think Twice (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Third Courier, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thomas the Tank Engine 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thomas the Tank Engine (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thomas the Tank Engine & Friends - Pinball (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Three Bears, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Three Stooges, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Three Stooges, The (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunder Blade (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunder Blade (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunder Blade (USA) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunder Boy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunder Burner (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunder Jaws (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunder Strike (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunderbirds (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ThunderCats (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| ThunderCats (Europe) (Compilation - TenStar Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunderhawk AH-73M (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Thunderhawk AH-73M (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tiger Road (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tilt (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time and Magik (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Machine (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Race (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runner (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 1 - Gateways in Time (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 11 - The Steel City (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 12 - A Target for the Cyborg (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 13 - Cyberkiller (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 14 - Toraxid - War Star (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 15 - At the Speed of Light (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 16 - The Galaxy Emperor (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 17 - The Living Labyrinth (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 18 - The Killer Shadow (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 19 - The Nightmare Prince (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 2 - The Space Stone (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 20 - The Mountains of Death (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 21 - The Black Dragon's Course (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 22 - The Eternal Damned (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 23 - The Time Monarch (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 24 - Beyond all Dimensions (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 25 - The Lost Planet Earth (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 26 - The Time Warrior (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 27 - Red Night (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 28 - Beyond the End (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 29 - The Last Revelation (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 3 - The Big Run (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 30 - The Final Duel (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 4 - The Castle of Fear (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 5 - The Black Knight (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 6 - The Bewitched Forest (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 7 - In the Land of the Invaders (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 8 - The Impregnable Fortress (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Runners 9 - The Time Demon (Europe) (En,Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Scanner (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Time Soldier (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Times Crosswords, The - Volume 3 & 4 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Times of Lore (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tintin on the Moon (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tiny Skweeks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tip Off (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tip Off (Europe) (Fr,De,Es,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tip Trick (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Titan (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Titanic Blinky (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Titano (Germany) (Coverdisk) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Titus the Fox (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Toki (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tom and the Ghost (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tom & Jerry 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tom & Jerry (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tom & Jerry - Hunting High and Low (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tom Landry Strategy Football - Deluxe Edition (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tony and Friends in Kellogg's Land (Germany) (Coverdisk - Sundancer) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Toobin' (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Top Banana (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Top Gear 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Top Gear 2 (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Torch 2081 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tornado (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Torvak the Warrior (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Total Carnage - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Total Carnage (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Total Carnage (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Total Eclipse (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Total Football (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Total Recall (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Total Recall (Europe) (Compilation - 2 Hot 2 Handle) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tournament Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tower of Babel (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tower of Souls (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tower Toppler (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Toyota Celica GT Rally (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Toyottes, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tracers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tracker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Traders (Germany) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Traders (Germany) (En) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trained Assassin (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trainer, Der - Italia (Germany) (v2.40) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Transarctica (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Transarctica (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Transputor (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Transworld (Germany) (v2.03) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| TransWrite (Europe) (v2.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Traps 'n' Treasures (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Treasure Island Dizzy (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Treasure Trap (Europe) (v1.08) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Treble Champions 2 (Europe) (Compilation - Championship Challenge) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trex Warrior - 22nd Century Gladiator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trex Warrior - 22nd Century Gladiator (Europe) (Coverdisk - The One - Issue 59) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| TrianGO (Europe) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tricky-Quiky-Games - Die Suche nach den Verschollenen Seiten (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trinity (Europe) (Release 11) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Triple X (Europe) (Compilation - Amiga Star Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trivia Trove (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trivial Pursuit - A New Beginning (Europe) (Compilation - Arcade Action) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trivial Pursuit - A New Beginning (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trivial Pursuit - The Computer Game - Genus Edition (France) (Compilation - 10 Megahits Vol. 3) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trivial Pursuit - The Computer Game - Genus Edition (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Troddlers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trojan LightPen Driver and KwikDraw Program (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trolls (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trolls (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Trolls (Europe) (ECS) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turbo Cup (Europe) (v2.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turbo Cup (France) (v2.0) (Compilation - Sport's Best) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turbo (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turbo Jam (Europe) (Proto) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turbo OutRun (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turbo Trax (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turn 'n' Burn (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turrican | TBD | Operational (A/V smoke) | TBD | TBD | TBD | Operational (A/V smoke) | Operational (A/V smoke) | TBD | TBD | TBD |
| Turrican 3 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turrican (Europe) | TBD | Unsupported media (format pending) | TBD | TBD | TBD | Unsupported media (format pending) | Unsupported media (format pending) | TBD | TBD | TBD |
| Turrican-II-The-Final-Fight Amiga EN | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Turrican II - The Final Fight (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turrican II - The Final Fight (Italy) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turrican III - Payment Day (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Turrican (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Tusker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| TV Sports Baseball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| TV Sports Basketball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| TV Sports Boxing (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| TV Sports Football (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| TV Sports Football (Europe) (0407) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Twilight Zone, The (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Twinworld (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Two to One (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Twylyte (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Typhoon (Europe) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Typhoon (Europe) (v1.01) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Typhoon Thompson in Search for the Sea Child (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| U.N. Squadron (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| UFO - Enemy Unknown (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| UFO - Enemy Unknown (Europe) (En,Fr,De) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| UFO - Enemy Unkown - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ugh! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultima III - Exodus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultima IV - Quest of the Avatar (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultima V - Warriors of Destiny (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultima VI - The False Prophet (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultimate! Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultimate Ride, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultimate Soccer Manager (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ultimate Soccer Manager Update (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| UMS II - Nations at War (Europe) (v1.2 020391) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| UMS - Scenario Disk One - The American Civil War (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| UMS - Scenario Disk Two - Vietnam (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| UMS - The Universal Military Simulator (Europe) (v1.5) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Under Pressure (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Universal Warrior (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Universe 3 (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Universe (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Unreal (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Unreal (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Untouchables, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Uridium 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Uridium 2 (Europe) (v1.04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Uridium 2 (Europe) (vP1.01) (D1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Utopia - Scenario Disk - The New Worlds (Europe) (Addon) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Utopia - The Creation of a Nation (Europe) (En,Fr,De,It) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Utopia - The Creation of a Nation (Europe) (En,Fr,De,It) (v3.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vade Retro Alienas (Europe) (Proto) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vaders (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Valhalla - Before the War (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vampire's Empire + Clever & Smart (Europe) (En,Fr,De) (Compilation - Amiga Star Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vampire's Empire (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vaxine (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vectorball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vengeance of Excalibur (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Venus - The Flytrap (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Venus - The Flytrap (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vermeer (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Veteran (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Victor Loomes - Das Adventure-Game (Germany) (Promo) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Video Creator - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| VideoKid (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vigilante (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vindex (Europe) (v1.0) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vindicators (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Violator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Violator + Super Grand Prix (Europe) (Compilation - Quattro Power Machines) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Virocop (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Virocop (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Virtual Karting (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Virus (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vision - The 5 Dimension Utopia (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vital Light - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vital Light (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vixen (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Viz - The Game (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Volfied (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Volleyball Simulator (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Voodoo Nightmare (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Voodoo Nightmare (Europe) (Budget - GBH) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vortex (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Voyager (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Voyager (Europe) (En,Fr,De) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Voyages of Discovery (Europe) (v1.00 20.2.1995) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Voyageurs du Temps, Les - La Menace (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vroom (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vroom Multi-Player (Europe) (Coverdisk - Amiga Dream - Issue 22) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vyper (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Vyrus (Europe) (Coverdisk - Amiga Fun - Issue 04) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wacky Races (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Walker (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wanderer 3D (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wanted (Europe) (Compilation - Commandos) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| War in Middle Earth (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| War in the Gulf (Europe) (En,Fr,De,Es) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| War Machine (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| War Machine (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| War Zone (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wargame Construction Set (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warhead (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warlock's Quest (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warlock's Quest (Europe) (Compilation - Super Quintet) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warlock the Avenger (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warlock (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warlords (Europe) (v2.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warp (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warp (Europe) (Compilation - The First Year) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warriors of Releyne (Europe) (En,Fr,De,Es,It) (v1.00) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Warzone (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Watchtower (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Waterloo (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Waterloo (Europe) (Compilation - Turning Points) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Waxworks (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Waxworks (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Waxworks (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Way of the Little Dragon, The (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Way of the Little Dragon, The + Spinworld (Europe) (Compilation - Amiga Star Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wayne Gretzky Hockey 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wayne Gretzky Hockey (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ween - The Prophecy (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Weird Dreams (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Welltris (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wembley International Soccer (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wembley Rugby League (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wendetta 2175 - CD32 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Western Games (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whale's Voyage 2 (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whale's Voyage (Europe) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whale's Voyage (Germany) (v1.2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whale's Voyage (Germany) (v1.2) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| When Two Worlds War (Germany) (v1.01) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| When Two Worlds War (Germany) (v1.01) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Where in the World is Carmen Sandiego (USA) (v1.1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whirligig (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| White Death - Battle for Velikiye Luki, November 1942 (Europe) (v1.3.6) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whizz (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whizz (Europe) (AGA) (Amiga 1200 Bundle - Amiga Magic) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Whizz (Europe) (ECS) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Who Framed Roger Rabbit (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wicked (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wicked (Europe) (Compilation - Power Hits) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wikinger + Bliff + Quadriga (Germany) (Coverdisk - Amiga Spiele 1) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wild Cup Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wild Streets (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wild West World (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wild Wheels (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Willi Lemke's Fussballmanager (Germany) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Willow (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wind in the Willows (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wind Surf Willy (Europe) (Hits for Six - Volume 7) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wind Surf Willy (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Window Wizard (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Windwalker (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wing Commander (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wing Commander (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wings (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wings of Death (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wings of Fury (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winter Camp (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winter Challenge - World Class Competition (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winter Games (Europe) (Compilation - Winter Gold) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winter Olympiad 88 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winter Olympics (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winter Supersports 92 (Europe) (Compilation - Mega Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winzer (Germany) (v1.13) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Winzer (Germany) (v1.14) (Compilation - World of Business) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wipe Out (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wishbringer - The Magick Stone of Dreams (Europe) (Release 69) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Witness (USA) (r22) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wiz 'n' Liz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wizard Warz (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wizardry - Bane of the Cosmic Forge (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wizardry - Bane of the Cosmic Forge (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wizball (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wizball (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wizball (Europe) (Compilation - TenStar Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wizkid (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wolfchild (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wolfpack (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wonder Dog (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wonderland (Europe) (v1.27i) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Woody's World (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Championship Boxing Manager (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Championship Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Class Rugby '95 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Class Rugby (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Cricket (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Cup - All Time Greats (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Cup Soccer - Italia '90 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Cup Soccer - Italia '90 (Europe) (Compilation - Sports Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Cup USA 94 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Darts (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Games (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World of Soccer (Europe) (Compilation - Championship Challenge) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Rugby (Europe) (Compilation - Top 15 Spielesammlung) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Soccer (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| World Tour Golf (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Worlds of Legend - Son of the Empire (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Worms (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Worms - The Director's Cut (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wrangler (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wrath Of The Demon - CDTV | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wrath of the Demon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Wreckers (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| WWF European Rampage Tour (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| WWF European Rampage Tour (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| WWF WrestleMania (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| WWF WrestleMania (Europe) (Budget - The Hit Squad) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| X-It (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| X-Out (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| X-Out (Europe) (Budget - Kixx) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenomorph (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenomorph (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenon-2-Megablast Amiga EN | TBD | Operational (disk smoke) | TBD | TBD | TBD | Operational (disk smoke) | Operational (disk smoke) | TBD | TBD | TBD |
| Xenon 2 - Megablast (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenon 2 - Megablast (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenon 2 - Megablast (Europe) (Compilation - Brainblasters) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenon 2 - Megablast (Europe) (Compilation - The Power Pack) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenon (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenon (Europe) (Compilation - Precious Metal) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xenophobe (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xiphos (Europe) (En,Fr,De) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| XP8 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| XR35 Fighter Mission (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| XR35 Fighter Mission (Europe) (0034) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| XR35 Fighter Mission (Europe) (Alt) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| XTreme Racing (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Xybots (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Yo! Joe! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Yogi Bear & Friends in the Greed Monster (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Yogi's Big Clean Up (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Yogi's Great Escape (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Yolanda (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Z-Out (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zak McKracken and the Alien Mindbenders (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zak McKracken and the Alien Mindbenders (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zak McKracken and the Alien Mindbenders (Germany) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zany Golf (USA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zarathrusta (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zargon (Germany) (Coverdisk - Amiga Software Extra Nr. 12 - Disk 2 of 2) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zeewolf 2 - Wild Justice (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zeewolf (Europe) (v1.02) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Ziriax (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zombi (France) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zone Warrior (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zool 2 (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zool 2 (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zool 2 (Europe) (AGA) (Amiga 1200 Bundle - Computer Combat) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zool - Ninja of the 'Nth' Dimension (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zool - Ninja of the 'Nth' Dimension (Europe) (AGA) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zool - Ninja of the 'Nth' Dimension (Europe) (Compilation - Award Winners - Gold Edition) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zoom! (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zork I - The Great Underground Empire (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zork II - The Wizard of Frobozz (Europe) (v48) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zork Trilogy (USA) (Zork I r88, II v48, III r17) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zork Zero - The Revenge of Megaboz (USA) (r366) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zyconix (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zynaps (Europe) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Zynaps + Nebulus (Europe) (Compilation - Premier Collection) | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |

## Current Evidence

Evidence is local and data-gated. ROMs, disk images, and generated artifacts are
not committed.

| Date | Evidence |
| --- | --- |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -System amiga500` passed for Workbench 1.3 Boot ZIP, Turrican ZIP, AlienSyndrome ZIP, and X-Copy v5.21 direct ADF. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -System amiga500plus,amiga600` passed for Workbench 2.05 Boot ZIP, Turrican ZIP, and ProTracker v2.0a direct ADF. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 ... -System amiga500 -Rom D:\emu\amiga\adf\Turrican.zip -RequireRenderedAudio -AudioFrames 9000` passed with 3,949 frames containing rendered audio signal and peak absolute sample value 14,336. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1200 -MinimumHeadlessFps 50 -System amiga500 -RomDir D:\emu\amiga\adf -MaxSets 20` passed for the first 20 directory-scanned ZIP-wrapped ADF sets, from A1060 PC Sidecar Plus II System Disk through A2090 Rodime RO3055 ReInstall Disk. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1200 -MinimumHeadlessFps 50 -System amiga500 -RomDir D:\emu\amiga\adf -StartAfter "A2090 Rodime RO3055 ReInstall Disk (Commodore) (1989).zip" -MaxSets 5` passed for the next five ZIP-wrapped ADF sets, from A2090 Toshiba MK-134FA ReInstall Disk through A2320 Flicker-Fixer Test Disk. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1200 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500 -RomDir D:\emu\amiga\adf -StartAfter "A2320 Flicker-Fixer Test Disk (Commodore) (1990).zip" -MaxSets 10` classified A2386, A3000 Multimedia disks, A4091, and A570 as prompt-only, and disk-smoke-proved A3000 Burn-In 1.0, A3000 Burn-In Final, A3000 PCBA Test, and both A590 HD RAM Test entries. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1200 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500 -RomDir D:\emu\amiga\adf -MaxSets 25` re-audited the first 25 directory-scanned ZIP-wrapped ADF sets: 21 rows are disk-smoke-proven and A1942, both A2090 HD Install variants, and A2232 are prompt-only. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500` passed for Workbench 1.3 Boot and Turrican. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500plus,amiga600` passed for Workbench 2.05 Boot, Turrican, and ProTracker v2.0a. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -RequireRenderedAudio -AudioFrames 9000 -System amiga500plus,amiga600 -Rom D:\emu\amiga\adf\Turrican.zip` passed with 3,977 frames containing rendered audio signal and peak absolute sample value 14,336 on both models. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500 -Rom Bubble Bobble.zip,Budokan_disk1.zip,Budokan_disk2.zip` passed; the runner grouped Budokan as a two-disk launch set. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom emeraldMine.zip,Gauntlet II.zip,Golden Axe.zip` passed for all 9 model/title launches. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom D:\emu\amiga\adf\DungeonMaster.zip` passed for all three models after UAE-1ADF extended ADF support mounted the raw MFM track. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 60 -System amiga500 -Rom "D:\emu\amiga\Turrican (Europe).7z"` failed with an explicit recognized-media message: the archive contains `Turrican (Europe).ipf`, and IPF/CAPS flux decoding is not implemented yet. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom D:\emu\amiga\Turrican-II-The-Final-Fight_Amiga_EN.zip` passed for all three models. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom D:\emu\amiga\Xenon-2-Megablast_Amiga_EN.zip` passed for all three models after `-RequireDiskProgress` was corrected to accept completed reads with observed head movement. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1200 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500 -Rom <10 explicit ADF ZIP paths>` disk-smoke-proved A590 HD Setup, both Amiga BASIC variants, Arkanoid [NTSC], Arthur, Arthur [savegame], Alien Syndrome, Action Replay Datel, and Action Replay 4 BETA; Action Replay ALE reached the Kickstart 1.3 insert-disk prompt. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1200 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500plus,amiga600 -Rom <10 explicit ADF ZIP paths>` disk-smoke-proved the same A590/Action Replay/Amiga BASIC/Alien Syndrome/Arkanoid/Arthur slice on both A500+ and A600. |
| 2026-07-01 | `mnemos_player --system amiga500plus/amiga600 --rom "D:\emu\amiga\adf\Workbench 2.05 (37.71) - Boot (Commodore) (1992).zip" --frames 4500 --screenshot build/scratch/workbench205-*.png` reached a 640x256 Workbench 2.0 desktop on both models with matching pixel statistics, after the 1500-frame disk-smoke run proved boot/disk progress. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500` and the same command with `-System amiga500plus,amiga600` disk-smoke-proved Black Crypt, Cannon Fodder TRSI, Cannon Fodder 2 PDX, Desert Strike, Disk-O-Rogue, Pang-500-NTSC.adf, Pinball Dreams, and R-Type across A500/A500+/A600. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500` and the same command with `-System amiga500plus,amiga600` over the DM2/DUNEII/It Came from the Desert/Pirates/PowerMonger/Prince of Persia/P47/Rainbow-Islands/Reel slice disk-smoke-proved Dune II, It Came from the Desert, Pirates, and P47 Thunderbolt on A500/A500+/A600. Visual follow-up kept DM2 Skullkeep and Reel_2.adf as prompt-only media, and left PowerMonger, Rainbow Islands, Reel_1.adf, and Prince of Persia unpromoted due black frames, memory/error dialog, or corrupted output. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 4500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom "D:\emu\amiga\adf\Prince of Persia.zip"` passed mechanical disk-smoke gates but rendered persistent banded white-on-black corruption, so no compatibility cell was promoted. |
| 2026-07-01 | After correcting high-resolution DDF fetch accounting, `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 4500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom "D:\emu\amiga\adf\Prince of Persia.zip"` rendered a legible A500 trainer screen and promoted A500 to visual smoke. A500+/A600 still render horizontally corrupted high-resolution text, so those cells remain visual defects. |
| 2026-07-01 | Controlled Prince of Persia reruns with explicit Kickstart overrides showed the text corruption follows Kickstart 2.0 rather than the ECS/1 MiB model: A500+ with Kickstart 1.3 renders the trainer cleanly, while A500 with Kickstart 2.0 renders the same horizontal corruption as default A500+/A600. Targeted CPU/RAM traces show the trainer scans Kickstart ROM for a KS1 font signature (`00 18 6C 6C`); under KS2 the scan wraps and finds the same byte pattern in the trainer code at `$027850`, so the program uses its own bytes as glyph data. Default KS2 cells stay `Visual defect`; this is classified as route-specific media compatibility rather than an Agnus high-resolution fetch defect. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Arkanoid/Bubble Bobble/DungeonMaster/emeraldMine/Gauntlet II/Golden Axe/Pang/R-Type ADF ZIP slice>` passed 24 mechanical launches. Manual screenshot review promoted Bubble Bobble, Emerald Mine, and R-Type only on the visually plausible A500 route; kept Dungeon Master A500 as a 1 MiB requirement/error prompt; and marked Bubble Bobble, Emerald Mine, and R-Type on A500+/A600 as visual defects. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Arthur/Deep Core/Disk-O-Rogue/Golden Axe AGA/P.P. Hammer/PREY/Onescapee/Swords_of_Twilight_Disk1 ADF ZIP slice>` produced 15 successful mechanical launches and 9 unsupported archive-entry failures. Manual screenshot review promoted P.P. Hammer on A500/A500+/A600, Disk-O-Rogue on A500+/A600, and Swords of Twilight disk smoke on all three; Disk-O-Rogue A500 was marked as a visual defect. Deep Core, PREY, and Onescapee ZIPs contained CD image media rather than ADF/ADZ/IPF/HDF entries. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Alien Breed/Another World/Lemmings/Lotus Turbo Challenge 2/Pinball Dreams/Speedball 2/Superfrog root .7z slice>` failed with explicit IPF/CAPS messages; those rows are marked `Unsupported media (format pending)` on A500/A500+/A600. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Budokan/P.P. Hammer variants/Pang-AGA/PinballDreamsNTSC/Golden Axe AGA slice>` was rerun after fixing dotted-title artifact names. Manual screenshot review marked Budokan as visual-defect-only on all three, promoted only the A500 route for P.P. Hammer 1991 CSL, marked the h PRX variant as visual-defect-only, classified Pang-AGA as an A500 load error plus A500+/A600 white-screen defect, and left Golden Axe AGA unpromoted because it only reached a generic crack intro. PinballDreamsNTSC ZIP contains loose hard-drive install files rather than a supported image entry. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Indiana Jones and the Fate of Atlantis 11-disk set/Swords of Twilight 2-disk set/Black Crypt 3-disk set>` passed all 9 mechanical launches. Manual screenshot review promoted Black Crypt and Swords of Twilight to visual smoke on A500/A500+/A600, while Indiana Jones and the Fate of Atlantis stayed visual-defect-only on all three. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <P.P. Hammer 1992 budget ZIP/Cannon Fodder TRSI 3-disk ZIP set>` passed all 6 launches. Manual screenshot review kept P.P. Hammer budget as visual smoke and promoted Cannon Fodder TRSI to visual smoke on A500/A500+/A600. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Superfrog/Chaos Engine/Risky Woods/Gods/Syndicate/Fire & Ice/Lemmings/Dogs of War/Project-X/DuckTales root .7z set>` failed before emulation because every selected archive contained IPF entries; those rows are marked `Unsupported media (format pending)` for A500/A500+/A600 until IPF/CAPS flux decoding lands. |
| 2026-07-01 | Reran the requested batch after the Prince route classification. `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 4500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <P.P. Hammer 1992 budget ZIP/Cannon Fodder TRSI 3-disk ZIP set>` passed all 6 launches; visual review of `build/scratch/amiga-corpus/requested-adf-smoke-4500-contact.png` showed readable P.P. Hammer loader/prod screens and stable Cannon Fodder TRSI intro screens. The same root `.7z` set for Superfrog, Chaos Engine, Risky Woods, Gods, Syndicate, Fire & Ice, Lemmings, Dogs of War, Project-X, and DuckTales still fails before emulation with explicit IPF/CAPS unsupported messages. `Superfrog.lha`, `Syndicate.lha`, and `Syndicate (1).lha` fail with no supported ADF/ADZ/IPF/HDF media entries, so those LHA rows are classified as unsupported archive entries pending hard-drive/package mounting support. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 3000 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Roadwar 2000/Workbench 2.04 Boot/Workbench 2.1 Boot/ProTracker v3.62 AGA/ProTracker v4.0 Beta 2 AGA/Quik The Thunder Rabbit RAR>` passed 15 mechanical launches and failed Quik before emulation due no supported media entries. Screenshot contact sheet `build/scratch/amiga-corpus/latest-smoke-contact.png` promoted Roadwar 2000 on all three models and Workbench 2.04/2.1 on A500+/A600, kept Workbench 2.04 A500 and both ProTracker A500 routes as visual defects, classified Workbench 2.1 A500 as prompt-only, and classified both ProTracker A500+/A600 routes as error prompts. |
| 2026-07-01 | Chunked requested-title smoke runs across A500/A500+/A600 with `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <82 exact local media paths>` found 80 IPF-backed root `.7z` routes that fail before emulation with explicit `IPF/CAPS flux decoding is not implemented yet` messages, plus `Apidya - CD32.zip` and `psygnosis_demos_cdtv.rar` archives with no supported ADF/ADZ/IPF/HDF entries. Existing ADF-proven rows from the request list were not downgraded. |
| 2026-07-01 | `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Emerald Mine/Desert Strike/Cannon Fodder/Bubble Bobble/Arthur/Alien Syndrome/P47/Pang/Pinball Dreams/Pirates/R-Type/Rainbow Islands ADF ZIP slice>` produced 33 successful disk-smoke launches and 3 black-frame Rainbow Islands failures. Manual review of `build/scratch/amiga-corpus/requested-batch-1500-contact.png` promoted Desert Strike, P47, Pang-500-NTSC, and Pinball Dreams to visual smoke; kept Cannon Fodder and Emerald Mine as previously classified; reclassified Bubble Bobble, R-Type, Rainbow Islands, and Pirates A500 as visual defects; and classified Alien Syndrome A500 as a visual defect plus Alien Syndrome A500+/A600 as Workbench error prompts. The same request batch inspected additional root `.7z` media and marked the remaining exact IPF-backed rows as `Unsupported media (format pending)`. |
| 2026-07-02 | After high-resolution DDF start/stop quantization was added, `scripts/amiga/run-corpus-smoke.ps1 -BuildDir build/windows-msvc-release -BiosDir D:\emu\amiga\bios -Frames 1500 -MinimumHeadlessFps 50 -RequireDiskProgress -RejectKickstartPrompt -System amiga500,amiga500plus,amiga600 -Rom <Bubble Bobble/R-Type ADF ZIP slice>` passed all 6 launches. Manual review of `build/scratch/amiga-corpus/ddf-quantization-contact.png` showed readable Bubble Bobble and R-Type trainer/crack screens on A500/A500+/A600, so those aggregate ADF rows are promoted to visual smoke. |

## Update Rules

- Keep rows alphabetized within `Applications` and `Games`.
- Add a new title row when it appears in the local Amiga media roots, enters
  the compatibility harness, or becomes a targeted parity investigation.
- Prefer the most specific title/version label available from the media name.
- Do not upgrade a cell from `TBD` without current-build evidence and a matching
  evidence row.
- Use `Operational (smoke)` for load/video/speed proof only. Use
  `Operational (disk smoke)` when `-RequireDiskProgress -RejectKickstartPrompt`
  is present. Use `Operational (visual smoke)` only after screenshot review,
  and use `Operational (A/V smoke)` only when rendered-audio evidence is present
  too.
