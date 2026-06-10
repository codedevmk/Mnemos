# Metrics

Instrumentation home for the context-engineering pilot
(`constitution/MNE-CTX-PLAN-001.md` §10, §13).

- `snapshots/` — point-in-time metric captures. `week0.json` is the pilot
  baseline; nightly `YYYY-MM-DD.json` snapshots begin in P3 when `metrics.py`
  lands.
- `DASHBOARD.md`, `packets/` — generated in P3; nothing here is hand-assembled.

## Week-0 baseline capture (P0)

The plan's epistemic rule forbids synthetic baselines: a value is either
measured or `pending`. Repository-derivable values in `week0.json` were
computed at capture time by the governance tools. The three session metrics
require instrumented agent sessions and stay `pending` until captured:

| Metric | Raw input | Procedure |
|--------|-----------|-----------|
| M6 tokens per accepted change | session token totals | Enable Claude Code OpenTelemetry export (decision D8); run one ordinary task per pilot subsystem (`src/chips/cpu/z80`, SMS machine) **without** capsules or task contracts; record total session tokens and whether the change merged without fixup. |
| M7 context-escape rate | file-read log | Same sessions, with a `PostToolUse` hook appending every file read to a log; the read manifest does not exist yet at week 0, so record the raw read set — the escape rate is computed retroactively once contracts define manifests. |
| M15 onboarding reads | file-read log | From the same log: count files read before the session's first edit. |

Store raw inputs alongside the snapshot (e.g. `snapshots/week0-session-z80.log`)
and fill the `value` fields in `week0.json` in the same commit. Hypotheses
H1–H4 (§13) are evaluated against these numbers at P3 + 4 weeks; until they
are filled, H1/H2/H4 cannot be evaluated and the pilot exit clock does not
start.
