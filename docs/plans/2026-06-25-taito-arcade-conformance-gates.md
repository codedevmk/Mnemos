# Taito Arcade Conformance Gates

Date: 2026-06-25

Scope: Taito arcade board work, starting with Taito F2 and applying to future
Taito X, H, L, G-NET/ZN-derived, Type X/Type Zero, and other Taito board-family
ports.

## Problem

The Taito F2 bring-up reached a partially playable state before all custom-chip
placement and timing rules were written down as acceptance criteria. Growl then
surfaced issues that should have been planned gates:

- TC0100SCN text-plane and BG-plane visible-area X offsets differ.
- TC0100SCN positive and negative text-scroll cases need separate coverage.
- Title-card, score HUD, high-score, gameplay, and attract scenes exercise
  different combinations of scroll values and plane contents.
- YM2610 ADPCM-A fixed-rate playback must be validated against the YM output
  cadence; a channel can sound present while still playing at the wrong rate.

The correction is to require chip and board conformance gates before any board
family is called working.

## Board-Family Gate

Every new Taito board family must have a tracked conformance capsule before
implementation moves past boot/display smoke:

- Board roster: CPU, sound CPU, tile/sprite/ROZ/video customs, audio customs,
  palette/custom priority chips, protection/security devices, storage media,
  and known variants.
- Reference sources: manuals, schematics, PCB observations, MAME behaviour as
  reference material, and local ROM/package evidence. Do not copy source; record
  provenance in `THIRD-PARTY-REFERENCES.md` when external projects inform
  behaviour.
- Register map: address windows, bit meanings, latches, IRQ lines, DMA paths,
  bank selectors, and reset defaults.
- Visual gates: per-plane scroll origin, visible-area offsets, flip offsets,
  rowscroll/colscroll, zoom, ROZ transform, sprite list format, buffering,
  priority/blend, palette format, and rotation.
- Audio gates: chip clock, output sample rate, sub-block cadence, IRQ/timer
  cadence, sample ROM addressing, looping/end behaviour, pan/level paths, and
  real captured scene evidence.
- State gates: deterministic save/load of all board and chip state, including
  pending DMA, audio decoder phase, timers, latches, buffered sprites, and
  external-media identity checks.
- Player gates: launch by canonical set name and by local package path,
  screenshot/audio extraction, service/test controls, coin/start inputs,
  save/load, and capability discovery.

## Taito F2 Gate

Taito F2 cannot be marked complete until these are all green:

- TC0100SCN: BG/text RAM layout, text glyph decode, BG/text visible-area X
  offsets, positive and negative scroll-Y origin, rowscroll, colscroll,
  layer-disable, priority swap, flip-screen offsets, and dual-TC0100SCN merge.
- TC0200OBJ/TC0190FMC: sprite record decode, bank routing, extension RAM
  policies, active-area markers, master/extra/absolute scroll markers, zoom,
  continuation chaining, buffering policies, disable/flip-screen state, and
  board hide-pixel offsets.
- TC0280GRD/TC0430GRW: ROZ RAM layout, control registers, fixed-point
  transform, board offsets, priority, and palette banking.
- TC0480SCP: four BG planes, RAM text plane, rowscroll, colscroll, layer zoom,
  row zoom, double-width mode, priority model variants, text/BG offsets, and
  flip offsets.
- TC0360PRI-style priority: text/BG/ROZ/sprite priority registers, sprite
  priority groups, and blend modes with real-scene proof.
- YM2610: SSG, FM, ADPCM-A, ADPCM-B, timers, IRQ, register traces, sub-block
  output cadence, sample ROM addressing, level/pan, audio capture, and
  save-state phase.
- Real ROM matrix: at minimum Growl, Dino Rex, Gun Frontier, Thunder Fox, Liquid
  Kids, Metal Black, Football Champ, Dead Connection, Dondoko Don, Pulirula,
  Ninja Kids, and one quiz title when local ROMs are available.
- Golden capture set: boot/title, high-score, attract/gameplay, scene with
  rowscroll/colscroll, scene with sprite priority/blend, and a representative
  audio-loop scene. Each capture stores raw framebuffer/audio plus register and
  RAM sidecars under `build/scratch` or CI artifacts, never in the repo root.

## Current F2 Audit Findings

The current F2 branch is partial, not complete. These are the missing gates most
likely to produce runtime surprises before a game-specific bug report lands:

- F2 video register sidecars now expose the active tilemap variant, palette
  selector, sprite mode, sprite active-area source, sprite buffering policy,
  TC0100SCN offsets and text-Y origins, TC0480SCP offsets, ROZ offsets, and
  sprite hide-pixel settings. This closes the immediate observability gap that
  made Growl title placement hard to inspect from screenshots alone.
- TC0100SCN origin coverage is still narrow. BG/text X offsets and positive vs
  negative text-scroll are now explicit, but BG Y origin, flip-screen text/BG
  offsets, dual-chip tie-breaking, and real rowscroll/colscroll scenes still
  need capture-backed tests.
- TC0200OBJ/TC0190FMC sprite handling is still the highest visual-risk area.
  The renderer covers bank routing, continuation, zoom, buffering, active-area
  markers, hide-pixel offsets, and priority groups synthetically, but Growl
  still shows misplaced/black sprite cells in real scenes. The next gate needs
  object-list sidecars from rendered frames and board-by-board sprite-family
  profiles.
- TC0200OBJ control-marker state now has a chip-owned
  `sprite_control_state_v1` sidecar. It records sprite mode, active-area
  source, buffer policy, final active area, disable/flip state, master scroll,
  and counts for control markers, disable/flip markers, active-area switches,
  master-scroll markers, extra-scroll markers, and blank records. The corpus
  runner observes `tc0200obj_control_marker_per_map` only when this sidecar is
  present with board-profile evidence and marker counts from a real capture.
  The latest Dino Rex smoke at
  `build/scratch/taito-f2-corpus/20260625-124822-475/` produced the 64-byte
  sidecar and observed 3 control markers, 1 master-scroll marker, and 2
  extra-scroll markers.
- TC0200OBJ buffering now has a chip-owned `sprite_buffer_state_v1` sidecar.
  It records the selected buffering policy, source RAM presence, delayed-source
  use, current-to-latched/current-to-delay copy paths, per-object word overlay
  mask/count, and derived byte counts. The corpus runner observes
  `tc0200obj_partial_buffer_byte_lane_profile` only when this sidecar agrees
  with the board profile; partial policies additionally require a nonzero
  overlay mask and delayed-source evidence.
- Banked TC0200OBJ blank-record handling is a required gate. A fully zero raw
  object record must stay empty before bank application; otherwise an active
  TC0190FMC bank turns blank RAM into real tile codes and produces black or
  misplaced cells in real scenes.
- Empty TC0200OBJ records also terminate any active multi-cell sprite sequence.
  They cannot simply be skipped while leaving continuation latches live, because
  the next non-empty record can otherwise inherit stale big-sprite placement.
- TC0360PRI-style priority and blend are still synthetic. A real capture matrix
  must prove text, BG, ROZ, and sprite priority interactions, including cases
  where the text layer should not simply behave as an always-top overlay.
- TC0480SCP and ROZ variants are first-pass. Metal Black, Football Champ, Dead
  Connection, Dondoko Don, and Pulirula need real screenshots for BG/text
  offsets, row zoom, double-width, priority model selection, ROZ transform, and
  flip offsets.
- YM2610 is not yet audio-complete. ADPCM-A output cadence is covered, the
  YM2610 register sidecar now exposes ADPCM-B status, cursor, start/end,
  DELTA-N, level, and accumulator state through the normal player audio JSON,
  and the F2 corpus runner can optionally export a rendered WAV/audio JSON and
  rendered-audio trace JSON, then summarize nonzero frames, peak level,
  active/silent windows, long silent runs, ADPCM-A active masks, ADPCM-A
  per-channel active/duty/end/underrun stats, ADPCM-A key on/off writes,
  ADPCM-B control/start/repeat-start writes, ADPCM-B active frames, loop/end
  events, and ROM underruns. The 2400-frame Growl probe produced valid nonzero
  audio, traced ADPCM-A active masks `0x08`, `0x10`, `0x18`, `0x20`, `0x28`,
  `0x30`, and `0x38`, saw no ADPCM-B starts, and saw no ADPCM ROM underruns.
  The latest 2400-frame Growl probes at
  `build/scratch/taito-f2-corpus/20260625-092404-655/`,
  `build/scratch/taito-f2-corpus/20260625-093732-969/`, and
  `build/scratch/taito-f2-corpus/20260625-094504-458/` also prove TC0140SYT
  command reads, Z80 NMI acceptance, and ADPCM-A key-ons are causally visible in
  the trace: 184 command-NMI pulses, 277 command reads, 184 Z80 NMI accepts,
  and command-read-to-ADPCM-A-key-on latency of 0-2 frames. It still flags a
  dropout risk: 181 active 100 ms windows, 45.25% active duty, 4200 ms longest
  post-active rendered silence, ADPCM-A active on only three channels, and
  18900 ms max per-channel post-active silence. That means the waterfall-style
  loop needs scene-timed waveform comparison before changing YM2610 sample
  decoding.
- TC0140SYT sound communication now has an executable command-delivery gate and
  a chip-owned register sidecar surface. A complete 68K command byte pulses the
  Z80 NMI, the sound CPU test proves a halted program consumes the command
  through the TC0140SYT data port, and debug dumps can inspect the selected
  ports, command/reply latches, pending bits, phase toggles, and command-NMI
  count. The remaining proof is real-program waveform evidence, not another
  implicit Growl polling assumption.
- TC0140SYT command consumption is now split from command posting. The latch
  sidecar records complete command writes, sound-Z80 command reads, reply
  writes, reply reads, clear writes, last command/reply bytes, and Z80 accepted
  NMI/IRQ counters in rendered-audio traces. The missing runtime gate is now
  explicit: a scene can show command-NMI pulses but still fail if command-read
  or Z80-NMI-accept evidence does not advance in time with the audio event.
- TC0140SYT main-port reset control is now a runtime sidecar gate instead of a
  hidden assumption. Main CPU writes through TC0140SYT port 4 drive the sound
  Z80 reset line, publish write/assert/release counts plus the mirrored Z80
  reset state in `sound_reset_state`, round-trip through F2 save-state, and
  expose observation rules for `sound_cpu_reset_control_line` and
  `tc0140syt_reset_callback_trace`. Real-ROM runs still need to observe an
  actual reset-control write before those gates count as scene evidence.
- F2 watchdog write-window handling is now explicit board state instead of a
  generic I/O side effect. The implementation maps confirmed watchdog writes
  for Quiz HQ (`0x580000`), Growl/Solitary Fighter (`0x340000`), Ninja Kids
  (`0x380000`), and Football Champ/Dead Connection (`0x800000`), tracks the
  Dino Rex `0xb00000` watchdog-like window as suspect, and records the
  priority-register writes that MAME labels watchdog-like for Football Champ,
  Dead Connection, and Ninja Kids as suspect diagnostics. The `watchdog_state`
  sidecar publishes write counts, confirmed/suspect counts, last address/value,
  active map/profile, present confirmed/suspect bases, and unsupported
  timeout/reset-model flags, then round-trips through save-state and the player
  memory-view adapter. This closes the previous uninstrumented gate for
  `watchdog_timer_reset_sidecar` and `watchdog_address_map_write_windows`, but
  exact timeout duration, read-reset semantics, and CPU reset consequences are
  still intentionally marked unsupported until real-program evidence exists.
- F2 main 68K bus observability is now a first-class diagnostic gate instead
  of a runtime surprise. The `main_bus_state` sidecar records byte-level access
  counts, inferred adjacent word pairs, odd-byte accesses, open-bus reads,
  unmapped writes, last mapped/open-bus classification, the active address map,
  and an explicit unsupported wait-state-model flag. This closes the previous
  uninstrumented class for bus/open-bus/byte-width tracing, but it does not yet
  claim exact 68K wait-state timing or per-device bus-stall duration.
- TC0110PCR palette support now has a compact palette state sidecar with the
  active board palette format/profile, last write address/value, resolved
  palette index, raw word, decoded RGB, write count, last readback
  address/value, readback count, readback raw word/index/RGB, adapter
  publication, and save-state coverage. Completion still needs real scenes
  proving each board-specific palette path, especially the sets whose sampled
  smoke frame does not read the palette window back.
- TC0220IOC/TC0510NIO/TE7750/TMP82C265-style I/O support now retains raw
  output-window writes, coin-meter edge counters, lockout latches, debug
  sidecars, save-state coverage, Growl-style four-player counter slots, a
  split-panel cabinet test mask, P3/P4 service-extension masks, and a
  Ninja Kids TE7750 auxiliary service/test input byte instead of dropping
  writes. It is still not chip-complete: exact per-chip output bit labels,
  watchdog timeout/reset side effects, DIP/service multiplexing, real
  Ninja Kids ROM evidence, and any protection/status readbacks need
  board-specific evidence before we call the input path complete.
- F2 I/O readback and byte-lane observability now has its own
  `io_access_state` sidecar. Input-window reads record DIP and service/system
  readback counts, even/odd byte lanes, inferred adjacent word pairs, last
  window/address/value, active map/profile ids, service masks, and split-panel
  state; output writes record matching even/odd and inferred-pair evidence.
  This closes the previous uninstrumented gates for
  `io_device_byte_lane_width_semantics` and `service_dip_mux_readback_trace`.
  Exact per-chip output bit names, protection/status semantics, and
  board-specific real-scene service/DIP assertions remain open.
- Frame-side object and priority evidence is improved but still not complete. A
  `decoded_sprite_objects_v1` sidecar records each rendered TC0200OBJ cell after
  bank, extension, continuation, zoom, hide-pixel, flip, active-area, and
  priority interpretation. A `priority_decisions_v1` sidecar now records every
  visible pixel's final source, final priority, sprite priority, attempted source
  mask, rejected source mask, final palette index, and last rejected source. The
  remaining gate is real-scene interpretation tooling plus a board-variant
  capture matrix proving the sidecar against TC0100SCN, dual-TC0100SCN,
  TC0480SCP, ROZ, and sprite/blend scenes.
- The data-gated F2 corpus runner now summarizes `priority_decisions_v1`,
  `decoded_sprite_objects_v1`, optional rendered-audio probes, and the new
  rendered-audio trace in `summary.json`. The current recursive local
  `D:\emu\arcade\Taito` 1200-frame smoke pass is green for `dinorex`,
  `gunfront`, `gunfrontj`, and `growl`, but that smoke frame only proves
  TC0100SCN BG/text final sources. It does not prove visible sprite priority,
  TC0480SCP, ROZ, blend interactions, or scene-correct audio loops; those
  require explicit attract/gameplay/audio capture points.
- The corpus runner also has an optional gameplay probe. With the default
  `select@30+4`, `start@180+8`, and `a@360+60` presses at 1800 frames, the
  local four-set probe still passes smoke 4/4. Current sidecar evidence:
  `growl` proves visible sprite final pixels (`sprite` final-source count 3856),
  49 decoded sprite objects, and 390 sprite occupancy rejects; `dinorex` decodes
  115 sprite objects but still has no visible sprite final-source or reject
  pixels at that capture point; `gunfront` and `gunfrontj` still show only
  TC0100SCN BG/text sources. This means the next real-scene gates need
  set-specific capture timings, not just a later generic frame.
- The corpus runner now records per-source bounding boxes from
  `priority_decisions_v1`. This is a required pre-runtime diagnostic for layer
  placement bugs: a scene can be nonblank while the text, secondary TC0100SCN,
  TC0480SCP text, ROZ, or sprite source is shifted too low, too far sideways, or
  completely outside the visible area.
- The corpus runner now emits `manifest-coverage.json` beside `summary.json`.
  This is the pre-runtime tripwire for the checked-in F2 roster: a local run
  must say which manifest sets, address-map variants, and feature gates were
  not exercised before we use a shared video/audio change as broad evidence;
  `MNEMOS_TAITO_F2_REQUIRE_MANIFEST_COVERAGE=1` makes that tripwire a failing
  gate. On the current local `D:\emu\arcade\Taito` corpus, 4 of 19 F2
  manifests are represented (`dinorex`, `growl`, `gunfront`, `gunfrontj`).
  The local `F2` subfolder only adds `gunfront` and `gunfrontj` wrappers; its
  other similarly named packages contain Irem M92 sets (`gunforce*`,
  `gunforc2`, `geostorm`) and are not Taito F2 coverage. The unchecked F2
  manifest sets remain `deadconx`, `dondokod`, `footchmp`, `liquidk`,
  `liquidku`, `metalb`, `mizubaku`, `ninjak`, `pulirula`, `qtorimon`,
  `quizhq`, `qzchikyu`, `qzquest`, `solfigtr`, and `thundfox`.
- The broad Taito inventory currently reports 4 of 25 local packages supported:
  the same F2 set slice above. The unsupported local packages are not hidden F2
  coverage; they route to G-NET, Type X/Type X2, Type Zero, Namco System 246,
  and non-Taito Irem M92 workstreams.
- Runtime feature evidence is now distinct from manifest coverage. The runner
  emits `runtime_feature_gates`, and
  `MNEMOS_TAITO_F2_REQUIRE_FEATURE_EVIDENCE=1` can make missing visual, audio,
  board-profile, input/I/O, and palette evidence fail. On a one-set Dino Rex
  probe, manifest coverage is 1/19 and the feature matrix correctly reports no
  runtime evidence yet for TC0480SCP, ROZ, dual-TC0100SCN, or banked TC0190FMC
  sprite scenes.
- The feature taxonomy now tracks baseline requirements as well as special F2
  variants. Every checked-in manifest contributes pre-runtime gates for
  TC0140SYT sound communication, YM2610 scene audio, vblank IRQ timing, and
  board input/DIP/watchdog behaviour; TC0100SCN sets add BG/text placement,
  scroll-origin, rowscroll/colscroll, and flip-offset gates; palette-format,
  sound-program, ADPCM-A, ADPCM-B, sprite-buffering, active-area, hide-offset,
  vertical-presentation, four-player, and shifted-quiz variants are modeled from
  the manifest. Most of these are intentionally still missing runtime evidence
  until the capture sidecars are added.
- The current pre-runtime review found additional easy-to-miss F2 gates before
  they surfaced as runtime bugs: vertical raw-vs-presented capture semantics for
  Gun Frontier, palette readback/provenance for the four palette formats,
  ADPCM-B loop diagnostics for sets with `adpcmb` ROMs, sound-Z80 program
  presence, sprite buffering policy evidence, and explicit service/test/coin
  counter/coin-lockout/watchdog sidecars for the I/O devices that are currently
  represented as board byte-return lambdas.
- The latest source review split several broad gates into named tripwires in
  the corpus taxonomy: board IRQ-level variants, M68000/Z80/YM2610 interleave,
  TC0100SCN BG Y origin and text priority, TC0480SCP row zoom/double-width,
  TC0480SCP priority/offset/flip variants, ROZ fixed-point offsets and palette
  banking, TC0200OBJ master/extra scroll, zoom continuation, and banked blank
  record handling. These are not all new bugs, but they were too easy to miss
  while the runner only reported broad `tc0100scn`, `tc0480scp`, `sprite`, and
  `audio` evidence.
- A second source review added pre-runtime tripwires for the remaining F2
  shortcuts visible in code: ROM interleave/decode provenance, M68000 IRQ
  acknowledge/vector timing, TC0140SYT nibble phase plus Z80 NMI acceptance,
  board/audio/video save-state phase, coin/service/watchdog/reset semantics,
  and the I/O custom variants currently represented as board byte-return
  lambdas (`TC0220IOC`/`TC0510NIO`, `TE7750`, and `TMP82C265`). These gates
  intentionally need non-video sidecars or real program evidence before they
  can pass.
- The same review split several renderer assumptions into explicit visual
  gates: tile graphics decode layout for TC0100SCN and TC0480SCP, distinct text
  source sidecars for RAM/program/TC0480SCP/secondary text, TC0200OBJ
  sprite-ROM region order, sprite palette-bank origin, extension-code timing,
  sprite disable/flip markers, offscreen wrap/clip behaviour, and board
  selectable sprite policy/buffering/active-area marker evidence. This is the
  path for catching black or misplaced sprite cells as a board-profile or
  object-decode mismatch instead of rediscovering the bug from screenshots.
- The corpus runner now parses the already-published `sound_bank_state`,
  `io_output_state`, and `palette_write_state` sidecars in addition to
  `board_profile_state`. Runtime feature rows can now observe configured
  sprite policies, sprite buffering variants, sprite active-area sources,
  TC0480SCP profiles, text-gfx sources, palette formats, input/I/O profiles,
  four-player profiles, I/O output/coin sidecars, Z80 sound-bank validity, and
  palette-write sidecars. `-RequireFeatureEvidence` is intentionally stricter:
  it now treats visual, audio, board-profile, input/I/O, and palette evidence
  classes as conformance gates rather than only checking visual gates.
- The review still leaves deliberate red gates for behaviour that current
  sidecars cannot prove: real watchdog reset timing, exact service/test/DIP
  multiplex readback, M68000 bus/open-bus/wait timing, raster mid-frame writes,
  and scene-specific YM2610 dropout proof beyond the current Growl
  command-latency trace. Palette readback is now observable, but still needs a
  broader set/scene matrix before it can be treated as globally proven. These
  should be implemented as explicit sidecars/tests before a later game report
  forces a less controlled fix.
  sprite-disable and flip-screen markers, offscreen wrap/clip rules,
  TC0190FMC bank-latch timing relative to sprite buffering, extension-code
  timing, and ROZ wrap/clip/flip semantics. These should be proven by sidecar
  plus scene evidence, not by a single nonblank frame.
- Audio coverage now also tracks what the current YM2610 implementation cannot
  yet prove: ADPCM-A per-channel cadence across more scenes, ADPCM-B loop/end
  cadence, pan/level mixing, audio decoder save-state phase, and broader
  Z80/NMI trace evidence tying real sound commands to the sound program. This
  keeps the Growl waterfall dropout from turning into an unscoped decoder tweak.
- The Growl 2400-frame audio trace shows ADPCM-A activity on channels 3, 4, and
  5 with no ADPCM ROM underruns, and ADPCM-B control writes only to `0x01`/`0x00`
  at frames 6, 206, and 599; there are no ADPCM-B start writes. The long silent
  spans at frames 1362-1487 and 2032-2299 also have no ADPCM-A key-on writes
  until playback resumes, so the current waterfall-dropout symptom is not proven
  to be a dropped decoder channel. The next gate is sound-program timing and
  TC0140SYT command/NMI scene evidence before changing YM2610 sample decoding.
- ADPCM-A re-key evidence is now a named runtime feature gate
  (`ym2610_adpcma_rekey_trace`). The corpus runner records per-channel key-on
  and key-off counts, key-on frame lists, first/last key-on frame, longest
  key-on gap, longest active run, longest silence after active, END events, and
  ROM underruns. This separates "sample ended and the sound program waited" from
  "decoder lost a channel" before future F2 audio reports turn into manual
  waveform archaeology.
- Rendered-audio traces now include TC0140SYT sound-communication registers
  beside YM2610 registers. The current 2400-frame Growl audio probe at
  `build/scratch/taito-f2-corpus/20260625-100615-771/` proves the command path
  is causally visible in the captured scene: 181 command writes, 272 command
  reads, 181 Z80 NMI accepts, 76 ADPCM-A key-ons, and
  command-read-to-ADPCM-A-key-on latency of 0-2 frames.
- The pre-runtime audit added narrower audio gates to prevent future runtime
  surprises: `tc0140syt_command_consumption_trace`,
  `tc0140syt_reply_clear_trace`, `z80_nmi_acceptance_trace`,
  `tc0140syt_to_ym2610_command_latency_trace`, and
  `m68k_z80_ym2610_interleave`. These gates require real trace evidence that
  the posted byte was consumed by the Z80, any reply/clear path behaved
  coherently, the Z80 accepted the NMI edge, and a downstream YM2610 key-on
  followed a consumed sound command. The current code can emit and observe the
  evidence for Growl; the remaining work is to run set-specific probes and pin
  scene expectations.
- TC0100SCN column-scroll and flip-screen coverage remain under-gated. The
  renderer has BG1 column-scroll plumbing and sprite flip tests, but the corpus
  does not yet require a real scene proving TC0100SCN BG/text flip offsets, BG
  Y origin, or column-scroll interaction with row-scroll and priority.
- TC0100SCN text Y placement is now explicit board/chip state instead of hidden
  renderer behaviour. The chip exposes separate non-positive-scroll and
  positive-scroll text origins through `SCNTYOD` and `SCNTYOP`, and F2
  save-state identity rejects mismatched origin profiles. Growl now declares
  `taito_f2_tc0100scn_positive_text_y_origin = 24`, which the local
  capture reports as `SCNTYOP=0x0018` and uses to keep the high-score/title text
  inside the 224-line frame. The remaining gate is broader real sidecar evidence
  for title, gameplay HUD, and high-score scenes across more F2 sets.
- Quiz-map text paths are no longer implicit RAM-glyph assumptions. `quizhq`
  and `qtorimon` now select a board-specific 1bpp text glyph source in the
  68000 program region, and chip/system/adapter tests prove the selected source
  and rendered pixels synthetically. The remaining gate is real local quiz-ROM
  evidence plus title/high-score screenshots to prove the program offsets
  against the shipped data.
- Text-source coverage is still per-variant, not universal. Primary
  TC0100SCN RAM text, primary TC0100SCN program text, TC0480SCP RAM text, and
  secondary TC0100SCN text must stay distinct in sidecars and golden captures;
  a correct HUD in Growl does not prove quiz text, dual-TC0100SCN text, or
  TC0480SCP title text.
- Display timing and visible area are hardcoded at 320x224 over a 512x262,
  60 Hz frame. F2 completion needs board/manual-backed refresh timing,
  blanking, raw-vs-presented orientation, and per-board visible-area offsets,
  otherwise placement fixes can be accidentally tuned to the SDL presentation
  instead of the emulated beam.
- Board-clock coverage was under-specified. The source review found the F2
  main-board clock profile should be treated as a gate: 68000 at 12 MHz, Z80 at
  4 MHz, and YM2610 at 8 MHz. The code now uses that 12/4/8 MHz profile, but
  real-game evidence still needs to prove frame pacing and interrupt cadence.
- IRQ/DMA coverage now has a first executable runtime gate. F2 boards have VBL
  and sprite-DMA request paths that usually land on IRQ5/IRQ6, with board jumper
  variation; the board model now keeps separate VBL and sprite-DMA pending
  state, asserts the highest pending M68000 IRQ line, classifies IACK by source,
  and raises sprite-DMA IRQs from board-level sprite latch paths. The remaining
  completion work is exact interrupt latency, bus-wait interaction, and
  board/manual proof for any non-IRQ5/IRQ6 jumper profile.
- A source review found a sharper IRQ tripwire: any F2 path that falls back to
  IRQ2 is a placeholder until a board-backed IRQ profile proves the actual
  VBL/DMA vectors and interrupt-ack timing per set. The checked-in F2 manifests
  now declare VBL IRQ5 and sprite-DMA IRQ6, and the corpus taxonomy tracks
  `m68k_irq2_placeholder_guard` to catch regressions.
- F2 IRQ evidence now has a board-level `irq_state` sidecar dumped with
  screenshots. It records configured VBL and sprite-DMA IRQ levels, the last
  VBL level asserted/acknowledged, VBL raise/ack counters, last sprite-DMA
  level asserted/acknowledged, sprite-DMA raise/ack counters, pending flags, and
  the current board IRQ line level. The corpus runner uses this sidecar for
  `m68k_autovector_irq_level_sidecar`, `m68k_irq2_placeholder_guard`,
  `vblank_irq_timing`, `vblank_irq_level_by_board`, and the IRQ5/IRQ6 mapping
  tripwire, plus `sprite_dma_irq_assert_ack_timing` and
  `tc0200obj_dma_irq_buffer_timing`. The current local recursive F2 smoke pass
  at `build/scratch/taito-f2-corpus/20260625-100739-334/` observed sprite-DMA
  IRQ6 assert/ack evidence for `dinorex`, `gunfront`, `gunfrontj`, and `growl`.
- Raster-time video writes remain under-specified. The board pushes TC0100SCN
  register state once per frame and renders from the vblank snapshot; it does
  not yet prove mid-frame scroll, priority, palette, sprite-latch, or IRQ
  effects. Add scanline/raster gates before trusting scenes that use row or
  priority effects.
- CPU/board timing is coarse. The current frame scheduler slices the 68000,
  Z80, and YM2610 without bus contention, wait states, exact interrupt latency,
  or sound-latch timing proof. That can hide races in games with tighter sound
  CPU polling or protection loops.
- Main/sound CPU bus ownership is still a model gap. The F2 system maps ROM,
  RAM, video, palette, input, and sound windows as direct byte handlers; it does
  not yet model custom-chip wait states, bus arbitration, access-width quirks,
  or per-board open-bus status beyond the explicit windows. Any future fix for
  audio cadence, protection loops, or mid-frame video writes must prove it is not
  depending on these shortcuts.
- Sound bank semantics now use an explicit low-bit Z80 latch decode, expose the
  raw/page/count/valid state as diagnostics, and read `0xFF` from missing pages
  instead of wrapping bad bank values into valid program bytes. Completion still
  needs real-board proof for any title-specific remap masks plus waveform
  evidence that the banked program path drives the expected YM2610 scenes.
- YM2610 FM is not yet chip-complete. The current implementation stores but
  does not decode LFO, AMS/PMS, SSG-EG, and CSM/mode-side effects. ADPCM
  diagnostics help the Growl waterfall case, but full F2 audio needs scene
  evidence for FM music, SSG effects, ADPCM-A, ADPCM-B, timers, IRQs, and
  save-state phase together.
- Inputs are boot/play focused. Service/test mode, DIP matrices, coin counters,
  coin lockouts, multi-player coin/service variants, and watchdog/reset
  behaviour need board-specific tests.
- I/O writes are now retained board state for the current F2 windows: raw
  output-register bytes, coin-meter edge counters, lockout lines, and compact
  debug sidecars are saved and exposed to the adapter. The remaining gate is
  exact TC0220IOC/TC0510NIO/TMP82C265/TE7750 bit semantics, watchdog/reset
  behaviour, DIP/service multiplexing, and any protection/status side effects
  proven by board-specific evidence.
- Four-player I/O is not equivalent to four-player coin output coverage. Growl
  and Ninja Kids now prove four-player input profiles separately from
  coin-counter/lockout slot coverage. The current sidecar now models four
  Growl-style and Ninja Kids TE7750 counter slots, four lockout bits, cabinet
  test masks, and P3/P4 service masks. The remaining four-player evidence gap is
  real Ninja Kids runtime capture: `four_player_coin_counter_lockout_outputs`,
  `four_player_coin_counter_slots`, and `four_player_service_test_mux` are
  unit-covered for the TE7750 path but still reported missing for Ninja Kids by
  corpus evidence until a local `ninjak.zip` run is available.
- Save-state compatibility is intentionally versioned for the new chip state
  layout. Old F2 save blobs are rejected; the completion gate is deterministic
  same-version save/load plus explicit rejection for incompatible board/media
  identity.
- Local ROM proof is still a small slice. The recursive local corpus currently
  proves only the supported F2 sets present under `D:\emu\arcade\Taito`; many
  local Taito packages are G-NET, Type X, Type Zero, System 246, or unrelated
  wrappers and require separate board-family gates.
- The current manifest roster spans many F2 board variants, not just Growl and
  Dino Rex: vertical Gun Frontier, TC0100SCN games, dual-TC0100SCN Thunder Fox,
  TC0480SCP games, ROZ games, TC0190FMC banked-sprite games, extension-sprite
  games, and shifted quiz maps. Each variant needs at least one runtime golden
  before we treat shared F2 changes as generally safe.
- The current checked-in F2 manifest roster is still not the full F2 board
  roster. Missing profiles include `finalb`, `megab`, `cameltry`/`cameltrya`,
  `ssi`/`majest12`, `mjnquest`, `yuyugogo`, `koshien`, `yesnoj`, `qjinsei`,
  `qcrayon`, `qcrayon2`, and `driftout`. Those introduce additional gates:
  TC0030CMD C-Chip protection, TC8521AP/RP5C01-style RTC and printer/status
  input, analog paddle input, mahjong key matrix input, alternate
  YM2203+OKIM6295 sound, Yuyugogo/Jinsei/Crayon sprite-extension profiles, and
  Drift Out/Cameltry ROZ/flip offset coverage.
- The corpus runner now reports that full-roster debt explicitly as
  `known_f2_roster_debt` in `manifest-coverage.json`. It lists the known F2
  sets that do not have checked-in manifests yet and the profile gates they
  imply (`tc0030cmd_cchip_protection`, `tc8521ap_rp5c01_rtc_status`,
  `printer_status_input`, `analog_paddle_input`, `mahjong_key_matrix_input`,
  `alternate_ym2203_okim6295_sound`,
  `yuyugogo_jinsei_crayon_sprite_extension_profiles`, and
  `driftout_cameltry_roz_flip_offsets`). This is report-only until those
  manifests exist, but it prevents the checked-in 19-set roster from being
  mistaken for the whole F2 platform.
- The F2 board profile is still too game-map-driven. `taito_f2_map` currently
  selects address windows and implicitly selects I/O custom, palette/priority
  custom, sprite custom pair, sound-communication custom, expansion video chip,
  and auxiliary device expectations. Split those physical-chip identities into
  manifest-visible profile fields before adding more F2 sets, otherwise a clone
  with the same memory map but different customs can pass the loader and fail
  later at runtime. The corpus taxonomy now tracks
  `f2_physical_chip_profile_manifest`, `f2_address_map_vs_chip_profile_split`,
  `io_custom_identity_profile`, and `aux_peripheral_protection_rtc_profile`.
- The current source review moved the next layer of runtime-surprise fields
  into manifest-visible declarations: player count, F2 input mux profile,
  TC0100SCN text graphics source/base, TC0100SCN BG/text X offsets,
  TC0480SCP board profile, and ROZ offsets. `taito_f2_map` should now be read
  as address-window selection, not permission to infer those behaviours for a
  future clone or new F2 set.
- The F2 custom-chip revision split is not modeled as a first-class board
  profile yet. Old boards use TC0200OBJ+TC0210FBC and TC0140SYT; newer boards
  use TC0540OBN+TC0520TBC and TC0530SYC. Mnemos currently routes all checked-in
  F2 sets through one TC0200OBJ-style renderer and one TC0140SYT-style sound
  comm chip, so the corpus taxonomy now tracks `f2_custom_chip_revision_profile`,
  `tc0200obj_custom_pair_old_new_versions`, and
  `tc0140syt_tc0530syc_sound_comm_profile`.
- TC0280GRD is still under-modeled for boards with more than one ROZ generator.
  Dondoko Don is currently represented through one shared ROZ RAM/control path,
  while the F2 roster includes dual-TC0280GRD boards. Add separate control/RAM
  sidecars and priority evidence for both chips before treating Dondoko Don or
  Cameltry-style ROZ scenes as covered. The corpus taxonomy now tracks
  `tc0280grd_multi_chip_register_pair`.
- Palette hardware is also collapsed too aggressively. The roster crosses
  TC0110PCR+TC0070RGB and TC0260DAR-style palette/mixer paths; Mnemos currently
  exposes board-selectable word formats, but it does not prove palette custom
  identity, readback, mixer side effects, or per-chip blanking behaviour. The
  taxonomy now tracks `tc0110pcr_tc0260dar_tc0070rgb_palette_profile`.
- The current embedded manifests still keep known TC0260DAR-style roster maps on
  the implemented TC0110PCR/TC0070RGB path, including the checked-in Dondoko Don,
  Thunder Fox, Liquid Kids/Mizubaku, Gun Frontier, Growl, Football Champ, Ninja
  Kids, Solitary Fighter, Quiz Quest, PuLiRuLa, Metal Black, Quiz Chikyu,
  Dead Connection, and Dino Rex maps. That is now an explicit failing gate via
  `tc0260dar_known_roster_profile` and `tc0260dar_runtime_support`; do not add
  more F2 sets or call the palette path complete until TC0260DAR behaviour,
  sidecars, and golden evidence exist.
- Vertical presentation is metadata-driven, but the headless screenshot dump
  writes the raw adapter framebuffer while the SDL player rotates vertical games
  at presentation time. Golden captures for Gun Frontier-style vertical F2 sets
  must record whether the artifact is raw or presented orientation.
- The latest F2 source review split another set of shared assumptions into
  explicit tripwires before they become per-game runtime bugs: M68000 bus
  wait/open-bus/access-width behaviour, Z80 sound-bank mask and page-size
  variants, video frame timing versus raw/presented visible area, TC0100SCN and
  TC0480SCP register-snapshot completeness, layer-disable/priority side
  effects, tile/sprite ROM layout provenance, TC0200OBJ decoded-object sidecar
  coverage, sprite graphics nibble/region order, and sprite palette-bank origin.
  These are now named in the corpus runner taxonomy so a local run can report
  missing evidence even when the game still boots.
- A follow-up F2 source review found that several register arrays need raw
  sidecar proof, not just derived chip state. The adapter now publishes raw
  byte views for `video_regs`, `secondary_video_regs`, `sprite_bank_regs`,
  `priority_regs`, `roz_control_regs`, and `tc0480scp_control_regs`, alongside
  existing RAM, palette, sound-bank, I/O, and palette-write sidecars. The
  corpus taxonomy now includes explicit gates for board raw register-window
  sidecars, TC0100SCN raw scroll registers, dual-TC0100SCN secondary registers,
  TC0480SCP raw controls, TC0360PRI raw priority registers, ROZ raw controls,
  TC0190FMC raw bank registers, and latched-vs-current TC0200OBJ RAM sidecars.
  The remaining work is to make scene-specific probes assert the raw sidecars,
  not just emit them.
- The same review found additional non-video gates that can fail before a
  screenshot looks wrong: M68000 autovector/IRQ-level sidecars, service/test
  input routing, per-set DIP defaults, watchdog timer/reset side effects, and
  clone/parent ROM resolution. These are now separate feature gates instead of
  being hidden inside broad input or ROM-loading coverage.
- Audio command delivery now has a causality gate for the Growl scene. TC0140SYT
  command/NMI counters, Z80 NMI acceptance, command reads, and YM2610 ADPCM-A
  key-on traces are connected by the corpus taxonomy, so waterfall/dropout
  reports can be diagnosed from a trace rather than by guessing at the decoder.
  The remaining audio gap is broader set/scene coverage and waveform comparison,
  not basic absence of command-to-key-on evidence.
- `tests/oracles/registry.yaml` still has no Taito F2 machine/chip entries.
  Add F2 video, sound-comm, YM2610-scene, and real-ROM golden entries when the
  capture matrix stabilizes, so the oracle ratchet can track regressions instead
  of relying on ad hoc corpus logs. The corpus taxonomy now tracks
  `f2_oracle_registry_entry`.
- The Growl black-cell issue currently classifies as final sprite output, not a
  backdrop or text-layer hole: priority sidecars show the obvious corruption is
  written by the TC0200OBJ path after tile layers are present. Draw-order
  reversal did not remove the artifact and broke an existing sprite-occupancy
  test, so the next fix should target sprite graphics layout, ROM region order,
  TC0190FMC bank timing, or decoded object state rather than global draw order.
- The current source review added a pre-runtime manifest support guard for the
  checked-in F2 roster. The parser can already describe forward-looking profile
  values (`TC0260DAR`, `TC0540OBN`/`TC0520TBC`, `TC0530SYC`,
  C-Chip/RTC/printer aux devices, and alternate IRQ levels), but the runtime
  still executes the implemented old-F2 path for those categories. Until each
  profile has behaviour, sidecars, and golden evidence, embedded F2 manifests
  must stay on `TC0110PCR`/`TC0070RGB`, `TC0200OBJ`/`TC0210FBC`,
  `TC0140SYT`, no aux device, VBL IRQ 5, and sprite-DMA IRQ 6.
- The same guard verifies partial manifest overrides keep map-derived defaults.
  A future manifest may override one chip/profile field without restating every
  F2 profile, but it must still inherit the map's input, I/O, video, priority,
  and TC0480SCP defaults. This prevents "one explicit field disables all board
  defaults" from becoming a hidden runtime failure.
- The follow-up review made the remaining profile knobs executable evidence.
  The adapter now exports a `board_profile_state` sidecar that records the active
  F2 map, chip/profile enums, IRQ levels, 12/4/8 MHz clock profile, raw versus
  presented capture flags, visible timing constants, Z80 bank mask/window/page
  shape, text/ROZ offsets, and an explicit runtime-support bit. The corpus
  taxonomy now consumes that sidecar for physical-chip profile, custom-chip
  profile support, board-clock profile, visible-area timing, vertical
  raw/presented capture, address-map/profile split, and Z80 sound-bank profile
  gates. Unsupported forward-looking profile values still remain behavior
  gaps; they are now visible before runtime instead of silently riding the
  old-F2 implementation path.
- Remaining unmodeled profile knobs found in source are still constants or
  diagnostics, not manifest-backed machine parameters: M68000/Z80/YM2610 clock
  profiles, Z80 bank mask/page window shape, raw-versus-presented visible area,
  bus wait/open-bus/access-width behaviour, watchdog reset timing, and
  TC0140SYT-to-YM2610 command causality. They are named in the corpus taxonomy,
  but not yet full runtime models.
- The latest source review split five broad tripwires into separate executable
  gates: `sound_cpu_reset_control_line`, `sprite_dma_irq_assert_ack_timing`,
  `cabinet_test_switch_input`, `four_player_coin_counter_slots`, and
  `four_player_service_test_mux`. The Growl-style input path now routes the
  frontend cabinet `test` switch to the split-panel system byte, routes P4
  service through the extension byte, and exposes sidecar masks for the cabinet
  test line and P3/P4 service lines. The Ninja Kids TE7750 path now exposes an
  auxiliary service/test byte at the unused input slot and maps frontend
  test/service controls to it. The sprite-DMA IRQ gate is now implemented and
  observed in the four local runnable F2 sets through the expanded `irq_state`
  sidecar. The reset gate now has a board-control path and `sound_reset_state`
  sidecar; it remains missing scene evidence until a real run observes a
  TC0140SYT port-4 reset-control write.
- Forward-looking F2 profile enums are now named as high-risk before a manifest
  uses them: `tc0530syc_runtime_support`, `tc0540obn_tc0520tbc_runtime_support`,
  `tc0030cmd_cchip_runtime_support`, `rtc_runtime_support`, and
  `printer_runtime_support`. None of the checked-in 19 manifests currently
  selects those profiles, but the parser can describe them, so they must fail as
  explicit audit debt rather than silently running through the old-F2 path.
- The pre-runtime F2 review now emits `pre_runtime_audit_debt` in
  `manifest-coverage.json`. The latest focused local passes at
  `build/scratch/taito-f2-corpus/20260625-093619-222/`,
  `build/scratch/taito-f2-corpus/20260625-093732-969/`, and
  `build/scratch/taito-f2-corpus/20260625-094504-458/` established the prior
  audit baseline. The current recursive local pass at
  `build/scratch/taito-f2-corpus/20260625-100739-334/` reports 126 feature
  gates with observation rules, 24 feature gates with no observation rule yet,
  zero TC0260DAR profile mismatches, and local sprite-DMA IRQ assert/ack
  evidence for all four runnable local F2 sets. The focused Growl audio probe at
  `build/scratch/taito-f2-corpus/20260625-100615-771/` observes the new
  M68000/Z80/YM2610 interleave and TC0140SYT-to-YM2610 latency gates. The prior
  17 checked-in TC0260DAR-era manifest profile mismatches are closed; the
  remaining audit target is behaviour evidence for the uninstrumented classes
  below.
- The latest recursive local pass at
  `build/scratch/taito-f2-corpus/20260625-101537-461/` keeps the same four-set
  smoke green and raises the executable observation-rule count to 127, with 23
  feature gates still lacking observation rules. `tc0110pcr_palette_readback`
  now has a runtime observation rule and was observed on `dinorex`, `gunfront`,
  and `gunfrontj` in that pass (`120`, `240`, and `240` palette readbacks
  respectively). `growl` wrote palette RAM heavily in the sampled frame but did
  not read the palette window back, so it remains a scene-coverage gap rather
  than an instrumentation gap.
- The same audit keeps several remaining traps visible before runtime:
  service/DIP/watchdog readback, M68000 exact IRQ latency, M68000
  exact wait-state timing, raster mid-frame video writes, ROZ
  fixed-point and priority/palette behaviour, TC0100SCN BG-Y/flip/rowscroll/text
  priority behaviour, TC0480SCP row-zoom/double-width/priority behaviour,
  TC0200OBJ hide pixels/blank-record/scroll-marker/zoom-continuation behaviour,
  YM2610 FM/timer/pan/save-state phase, sound CPU reset control, Ninja Kids
  four-player coin/service evidence, and unsupported alternate sound/sprite/aux
  profile behaviour. The remaining high-risk uninstrumented classes are now:
  raster mid-frame video writes, TC0200OBJ control-marker profiles,
  TC0200OBJ partial-buffer byte-lane profiles, per-board scene-capture matrix
  coverage, YM2610 scene-loop waveform comparison, TC0530SYC runtime support,
  TC0540OBN/TC0520TBC runtime support, and auxiliary protection/RTC/printer
  profiles.
- This review found additional F2 gates that were still too broad or only
  prose-backed in the earlier audit. The corpus taxonomy now names them as
  pre-runtime debt: TC0140SYT reset-callback trace, per-address-map watchdog
  write windows, M68000 byte/word access-width and unmapped/open-bus sidecars,
  I/O custom byte-lane semantics, service/DIP mux readback traces, per-board
  scene-capture matrix coverage, TC0200OBJ control-marker profiles, partial
  sprite-buffer byte-lane profiles, TC0100SCN/TC0480SCP scene bounding-box
  origin matrices, ROZ flip-scene offsets, YM2610 loop waveform comparison,
  and external DAC/filter-route profiling. They stay red until code-side
  evidence exists; later bullets record the subset that has since moved into
  executable sidecar evidence.
  A manifest-only runner function check after this edit sees 19 F2 profiles,
  164 named feature gates, 128 observation rules, and 37 unmapped gates before
  rerunning the real-ROM corpus.
- The main-bus follow-up moved M68000 byte/word access-width and
  unmapped/open-bus sidecars from prose debt into code-side evidence via
  `main_bus_state`. The runner now has observation rules for
  `m68k_bus_wait_open_bus_width`, `m68k_byte_word_access_width_trace`, and
  `address_map_unmapped_open_bus_sidecar`; exact wait-state timing remains an
  unsupported flag inside the sidecar until per-device stall timing is modeled.
- The I/O follow-up moved byte-lane and service/DIP readback tracing from
  prose debt into code-side evidence via `io_access_state`. The runner now has
  observation rules for `io_device_byte_lane_width_semantics` and
  `service_dip_mux_readback_trace`; per-chip output naming and protection/RTC
  readback behaviour remain separate gates.
- The current F2 source review leaves the checked-in 19-manifest roster on the
  implemented old-F2 profile path (`TC0200OBJ+TC0210FBC`, `TC0140SYT`, and no
  auxiliary profile), but keeps the unsupported forward-profile tripwires
  explicit before runtime. The current manifest coverage output from
  `build/scratch/taito-f2-corpus/20260625-112901-374/` reports 136 observation
  rules and 28 named gates with no observation rule yet. The remaining high-risk
  uninstrumented classes are still raster mid-frame video writes, TC0200OBJ
  control-marker profiles, TC0200OBJ partial-buffer byte-lane profiles,
  per-board scene-capture matrix coverage, YM2610 scene-loop waveform
  comparison, TC0530SYC runtime support, TC0540OBN/TC0520TBC runtime support,
  and auxiliary protection/RTC/printer profiles.
- The same four-set local runtime sweep passed `dinorex`, `gunfront`, and
  `growl`, but the full runner loop intermittently returned `save_exit=-1` for
  `gunfrontj` with no diagnostic beyond startup. Direct execution of the exact
  staged `gunfrontj.self.zip` plus synthesized parent `gunfront.zip` at 1200
  frames wrote a valid state, including with stdout/stderr redirection; directly
  loading that state for one frame produced a nonblank 320x224 screenshot.
  Treat `gunfrontj` as runner-loop stability debt until the corpus pass is
  repeatably green, not as completed runtime coverage.
- The scene-capture follow-up moved `scene_capture_matrix_per_board` out of the
  high-risk uninstrumented list. The runner now requires a nonblank screenshot,
  valid priority sidecar, board-profile sidecar, and visible-area sidecar before
  a board run can observe that feature. The focused local pass at
  `build/scratch/taito-f2-corpus/20260625-123326-197/` passed `dinorex`, raised
  the observation-rule count to 137, reduced the no-rule feature count to 27,
  and reports `scene_capture_matrix_per_board` observed for `dinorex` while the
  other manifest-backed sets remain missing observed scene coverage.

## Future Taito Boards

For Taito X, H, L, G-NET/ZN-derived, Type X/Type Zero, and other Taito board
families, do not start from generic arcade scaffolding alone. First create the
board-family gate above, then implement in this order:

1. Board capsule and custom-chip checklist.
2. Minimal loader/media identity and ROM/package matrix.
3. CPU/reset/vector proof with register trace.
4. Video custom-chip unit tests for offset/scroll/priority rules.
5. Audio custom-chip unit tests for clock/cadence/mixing rules.
6. Player adapter with screenshot/audio/save/load evidence.
7. Real-corpus smoke and golden captures.

## Completion Rule

For Taito arcade work, unit tests plus a nonblank screenshot are not enough.
The status stays partial until the board-family gate, chip-level conformance
tests, real-ROM capture matrix, and audio evidence are all present for the
target board and game set.
