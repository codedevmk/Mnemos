#!/usr/bin/env python3
"""M6 ingestion — the D8 fallback: parse a Claude Code session transcript.

Usage: ingest_session.py <transcript.jsonl> [--session-id ID]

Claude Code writes session transcripts as JSONL (one entry per message,
assistant entries carrying `message.usage` token counters). This extracts
the session's token totals and appends a `session_tokens` event to
metrics/sessions/<id>.jsonl, where metrics.py picks it up for M6.

Token accounting (documented heuristic, ADR-0017): billable effort =
input_tokens + cache_creation_input_tokens + output_tokens. Cache *reads*
are reported separately but excluded from the total — re-served context is
not new work. All four counters are recorded so the accounting can be
revisited without recapturing.

OTel export (decision D8) remains the recommended path; this exists so M6
is measurable today from the transcripts Claude Code already writes.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from claude_hooks import append

COUNTERS = ("input_tokens", "cache_creation_input_tokens",
            "cache_read_input_tokens", "output_tokens")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("transcript", help="path to a Claude Code session .jsonl")
    ap.add_argument("--session-id", help="override the session id (default: file stem)")
    args = ap.parse_args()

    path = Path(args.transcript)
    if not path.exists():
        print(f"error: {path} not found", file=sys.stderr)
        return 1

    totals = dict.fromkeys(COUNTERS, 0)
    messages = 0
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue
        usage = (entry.get("message") or {}).get("usage")
        if not isinstance(usage, dict):
            continue
        messages += 1
        for key in COUNTERS:
            totals[key] += int(usage.get(key) or 0)

    if messages == 0:
        print(f"error: no usage entries found in {path}", file=sys.stderr)
        return 1

    session_id = args.session_id or path.stem
    billable = (totals["input_tokens"] + totals["cache_creation_input_tokens"]
                + totals["output_tokens"])
    append(session_id, {"event": "session_tokens", "messages": messages,
                        "billable_tokens": billable, **totals})
    print(f"ingest_session: {session_id}: {billable} billable tokens "
          f"across {messages} usage entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
