---
id: ADR-0017
title: "P3 Operations — Implementation Decisions"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: 2026-06-10
---

# ADR 0017: P3 Operations — Implementation Decisions

## Context

MNE-CTX-PLAN-001 P3 specifies the metrics pipeline, the weekly packet with
its tuning loop, and the decision-capture ritual. Implementation decisions
worth recording:

## Decisions

1. **Session telemetry lands as gitignored JSONL, aggregated into committed
   snapshots.** `.claude/settings.json` (now tracked, via a narrow exception
   to the `.claude/` ignore) wires `PostToolUse` and `SessionEnd` hooks to
   `tools/governance/claude_hooks.py`, which appends read/edit events and
   capture-omission records to `metrics/sessions/*.jsonl`. Raw logs are
   working data, never authority; `metrics.py` folds them into the committed
   nightly snapshot. Hooks are logging-only and never block a tool or
   session.

2. **Unmeasurable metrics are emitted as null with the blocking reason,
   never estimated.** M6 awaits OTel ingestion (D8), M7 awaits task-contract
   read manifests in live use, M10 awaits per-vector harness JSON
   (ADR-0016), M14 awaits CI duration ingestion. The dashboard prints the
   reasons; the packet tracks H1/H2/H4 as blocked until week-0 sessions
   exist.

3. **M4 and M8 are computed by documented heuristics, trend-only.** M4 =
   1 − (verified invariants / RFC-2119 keyword lines) over constitution
   modules (risk R2 acknowledged); M8 = commits not named as
   revert/fixup/squash within a 14-day window. Both formulas live in
   `metrics.py`'s docstring and tune via normal PRs.

4. **The tuning loop auto-drafts mechanically.** `weekly_packet.py --draft`
   creates a `docs/adr/proposed/NNNN-tuning-*.md` for the single largest
   tracked regression, containing only the trigger (metric, prev → now);
   diagnosis and correction are the ratifier's. Rejection is a valid
   outcome; expiry applies as to any proposal.

5. **`/capture` is a tracked Claude Code command**
   (`.claude/commands/capture.md`) implementing §5.2 mechanism 1; the
   SessionEnd hook implements mechanism 2 (omission visibility, never
   auto-drafting); gate G10 remains mechanism 3.

6. **Cadence rides the nightly workflow**: the metrics job runs after the
   oracle ratchet and commits the snapshot + dashboard; the weekly packet
   generates on Mondays (or manual dispatch) with `--draft` enabled.

7. **M14 lands via CI-duration ingestion.** The nightly metrics job runs
   `tools/governance/ingest_ci_durations.py`, which queries the GitHub
   Actions API (`GITHUB_TOKEN` in CI; graceful no-op without one) for the
   trailing week of completed `ci` workflow runs and writes
   `metrics/ci_durations.json`; `metrics.py` computes the p50/p95 against
   the 5-minute PR-suite budget from that file.

8. **M6 lands via transcript parsing — the D8 fallback.** OTel export
   remains the recommended path, but `tools/governance/ingest_session.py`
   parses a Claude Code session transcript (JSONL) into a `session_tokens`
   event in `metrics/sessions/`, counting non-cache-read input +
   cache-creation + output tokens. `metrics.py` then reports M6 as total
   session tokens over commits in the trailing 7 days — a documented
   heuristic until sessions reference their merged changes explicitly.

## Consequences

- P3 exit criterion "2 consecutive packets with zero manual assembly" is
  now a matter of wall time; the first packet (2026-W24) generated clean.
- M16 reads 15 for the bootstrap week — far over the ≤ 6/week steady-state
  budget, as expected for the week the entire authority layer was ratified;
  the packet trend is the signal to watch.
- The hypothesis evaluation clock starts when the owner captures the week-0
  session baselines (see `metrics/README.md`).

## Ratification

Ratified 2026-06-10 by owner directive in the pilot session (amended with
decisions 7-8 prior to acceptance).
