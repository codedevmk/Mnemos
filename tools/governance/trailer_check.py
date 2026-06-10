#!/usr/bin/env python3
"""Gate G10: provenance trailers on authority-touching commits.

A commit that touches `CONSTITUTION.md`, `constitution/`, or wire schemas
(`src/debug/wire/`) must carry a trailer line:

    Refs: ADR-NNNN | ARCH-NNN | STD-NNN | MNE-...

Scope is deliberately narrow (MNE-CTX-PLAN-001 decision D4): trailers on every
commit would be ceremony, not signal.

Modes:
  --range A..B                 check each commit in the range (CI)
  --message-file F [--staged]  check one pending commit message against the
                               staged paths (commit-msg hook)

Exits 0/1.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCOPED_PREFIXES = ("CONSTITUTION.md", "constitution/", "src/debug/wire/")
TRAILER_RE = re.compile(
    r"^Refs:\s*(ADR-\d{4}|ARCH-\d{3}|STD-\d{3}|MNE-[A-Z0-9-]+)", re.MULTILINE
)


def git(*args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(REPO), *args], capture_output=True, text=True, check=True
    ).stdout


def touches_scope(paths: list[str]) -> bool:
    return any(p.startswith(SCOPED_PREFIXES) for p in paths)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--range", dest="rev_range")
    ap.add_argument("--message-file")
    ap.add_argument("--staged", action="store_true")
    args = ap.parse_args()

    errors: list[str] = []

    if args.rev_range:
        for sha in git("rev-list", args.rev_range).split():
            paths = git(
                "diff-tree", "--no-commit-id", "--name-only", "-r", sha
            ).split("\n")
            if touches_scope([p for p in paths if p]):
                message = git("log", "-1", "--format=%B", sha)
                if not TRAILER_RE.search(message):
                    errors.append(
                        f"{sha[:12]}: touches {'/'.join(SCOPED_PREFIXES)} "
                        "but has no `Refs: ADR-XXXX|ARCH-XXX` trailer"
                    )
    elif args.message_file:
        paths = git("diff", "--cached", "--name-only").split("\n") if args.staged else []
        if touches_scope([p for p in paths if p]):
            message = Path(args.message_file).read_text(encoding="utf-8")
            if not TRAILER_RE.search(message):
                errors.append(
                    "commit touches authority paths but has no "
                    "`Refs: ADR-XXXX|ARCH-XXX` trailer"
                )
    else:
        ap.error("one of --range or --message-file is required")

    for e in errors:
        print(f"error: {e}")
    print(f"trailer_check: {len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
