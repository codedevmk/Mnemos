#!/usr/bin/env python3
"""Gate G11 (advisory): token budgets for authority documents and capsules.

Budgets (MNE-CTX-PLAN-001 sections 4.2 and 6.1, decision D3):
  - CONSTITUTION.md                      <=  8,000 tokens
  - constitution/ARCH-*.md, STD-*.md     <= 12,000 tokens each
  - **/CAPSULE.md                        <=  6,000 tokens each

Token counts are estimated deterministically without a tokenizer dependency:
estimate = max(word_count, ceil(char_count / 4)). This over-counts slightly
for prose and is therefore conservative against the budgets.

Advisory by default (always exits 0; findings feed the weekly packet).
--strict exits 1 on any breach.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def estimate_tokens(text: str) -> int:
    return max(len(text.split()), math.ceil(len(text) / 4))


def budgeted_files() -> list[tuple[Path, int]]:
    files: list[tuple[Path, int]] = []
    index = REPO / "CONSTITUTION.md"
    if index.exists():
        files.append((index, 8000))
    cdir = REPO / "constitution"
    if cdir.is_dir():
        for p in sorted(cdir.glob("*.md")):
            if p.name.startswith(("ARCH-", "STD-")):
                files.append((p, 12000))
    for p in sorted(REPO.rglob("CAPSULE.md")):
        files.append((p, 6000))
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    rows = []
    breaches = 0
    for path, budget in budgeted_files():
        tokens = estimate_tokens(path.read_text(encoding="utf-8"))
        over = tokens > budget
        breaches += over
        rows.append(
            {
                "file": path.relative_to(REPO).as_posix(),
                "tokens": tokens,
                "budget": budget,
                "over_budget": over,
            }
        )
        marker = "BREACH" if over else "ok"
        print(f"{marker:6} {tokens:>6}/{budget:<6} {path.relative_to(REPO).as_posix()}")

    result = {"gate": "G11", "files": rows, "breaches": breaches}
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"token_budget: {breaches} breach(es)")
    return 1 if (args.strict and breaches) else 0


if __name__ == "__main__":
    sys.exit(main())
