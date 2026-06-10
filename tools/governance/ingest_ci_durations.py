#!/usr/bin/env python3
"""M14 ingestion: pull CI workflow durations from the GitHub Actions API.

Run by the nightly metrics job (GITHUB_TOKEN and GITHUB_REPOSITORY are set
there); writes metrics/ci_durations.json with per-run wall-clock seconds for
the trailing week of completed `ci` workflow runs. metrics.py computes the
M14 p50/p95 from this file against the 5-minute PR-suite budget.

Without a token or network (local runs), exits 0 without touching the file —
M14 stays null rather than stale or fabricated.
"""

from __future__ import annotations

import datetime as dt
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "metrics" / "ci_durations.json"


def main() -> int:
    token = os.environ.get("GITHUB_TOKEN")
    repository = os.environ.get("GITHUB_REPOSITORY")
    if not token or not repository:
        print("ingest_ci_durations: no GITHUB_TOKEN/GITHUB_REPOSITORY; skipping")
        return 0

    since = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=7)).isoformat()
    url = (f"https://api.github.com/repos/{repository}/actions/workflows/"
           f"ci.yml/runs?status=completed&created=>{since}&per_page=100")
    request = urllib.request.Request(url, headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
    })
    try:
        with urllib.request.urlopen(request, timeout=30) as resp:
            data = json.load(resp)
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as e:
        print(f"ingest_ci_durations: API unavailable ({e}); skipping")
        return 0

    runs = []
    for run in data.get("workflow_runs", []):
        started, updated = run.get("run_started_at"), run.get("updated_at")
        if not started or not updated:
            continue
        seconds = (dt.datetime.fromisoformat(updated.replace("Z", "+00:00"))
                   - dt.datetime.fromisoformat(started.replace("Z", "+00:00"))
                   ).total_seconds()
        runs.append({"id": run["id"], "event": run.get("event"),
                     "conclusion": run.get("conclusion"),
                     "seconds": round(seconds)})

    OUT.write_text(json.dumps(
        {"schema": "mnemos-ci-durations/1",
         "captured": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
         "window_days": 7, "runs": runs},
        indent=2) + "\n", encoding="utf-8")
    print(f"ingest_ci_durations: {len(runs)} run(s) recorded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
