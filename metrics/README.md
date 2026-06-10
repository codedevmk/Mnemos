# Metrics

Instrumentation home for the context-engineering pilot
(`constitution/MNE-CTX-PLAN-001.md` §10, §13). Everything in this tree is
generated; nothing is hand-assembled.

- `snapshots/` — `week0.json` is the pilot baseline; `YYYY-MM-DD.json` are
  nightly snapshots written by `tools/governance/metrics.py`.
- `DASHBOARD.md` — regenerated with every snapshot.
- `packets/YYYY-WW.md` — weekly review packets
  (`tools/governance/weekly_packet.py`), the H1 work surface: metric deltas,
  proposal expiry countdowns, advisory findings, hypothesis status, and the
  tuning loop (largest regression + auto-drafted corrective proposal).
- `sessions/` — raw per-session JSONL telemetry written by the Claude Code
  hooks (`.claude/settings.json` → `tools/governance/claude_hooks.py`):
  file reads/edits (M7/M15 inputs) and capture-omission records (§5.2).
  Gitignored; `metrics.py` aggregates it into committed snapshots.

## Week-0 baseline capture (P0)

The plan's epistemic rule forbids synthetic baselines: a value is either
measured or `pending`. Repository-derivable values in `week0.json` were
computed at capture time. The three session metrics require instrumented
sessions and stay `pending` until captured:

| Metric | Raw input | Procedure |
|--------|-----------|-----------|
| M6 tokens per accepted change | session token totals | Enable Claude Code OpenTelemetry export (decision D8); run one ordinary task per pilot subsystem (`src/chips/cpu/z80`, `src/manifests/sms`) **without** capsules or task contracts; record total session tokens and whether the change merged without fixup. |
| M7 context-escape rate | file-read log | The `PostToolUse` hook now logs reads automatically to `sessions/`; week-0 sessions record the raw read set, scored retroactively once task contracts declare read manifests. |
| M15 onboarding reads | file-read log | From the same logs: reads before the session's first edit (`metrics.py` computes the median automatically). |

Fill the `value` fields in `week0.json` from those sessions in one commit.
Hypotheses H1–H4 (§13) are evaluated at P3 + 4 weeks against these numbers;
until they exist, H1/H2/H4 cannot be evaluated and the pilot exit clock does
not start. H3 (M8) is measured from git alone and is already live.
