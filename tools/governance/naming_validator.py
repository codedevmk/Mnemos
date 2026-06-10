#!/usr/bin/env python3
"""Gate G1 (naming): validate source-file naming rules (STD-001).

Checks, over git-tracked C++ sources:
  - basenames are snake_case: [a-z0-9_]+ plus extension
  - header basenames are globally unique across the repository
    (TDS section 5.1: headers are included by basename, so collisions
    silently shadow each other on the include path)

Deterministic; exits 0 on zero findings, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HEADER_SUFFIXES = {".hpp", ".h", ".hh", ".hxx"}
SOURCE_SUFFIXES = HEADER_SUFFIXES | {".cpp", ".cc", ".cxx", ".c"}
SNAKE_CASE = re.compile(r"^[a-z0-9_]+\.[a-z]+$")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", "-z"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    tracked = [Path(p) for p in out.split("\0") if p]

    findings: list[dict] = []
    headers_by_name: dict[str, list[str]] = defaultdict(list)

    for path in tracked:
        if path.suffix not in SOURCE_SUFFIXES:
            continue
        if not SNAKE_CASE.match(path.name):
            findings.append(
                {
                    "file": path.as_posix(),
                    "rule": "snake-case",
                    "message": f"`{path.name}` is not snake_case",
                }
            )
        if path.suffix in HEADER_SUFFIXES:
            headers_by_name[path.name].append(path.as_posix())

    for name, paths in sorted(headers_by_name.items()):
        if len(paths) > 1:
            findings.append(
                {
                    "file": paths[0],
                    "rule": "unique-header",
                    "message": f"header basename `{name}` occurs {len(paths)} times: "
                    + ", ".join(paths),
                }
            )

    result = {"gate": "G1-naming", "findings": findings}
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(result, indent=2), encoding="utf-8")
    for f in findings:
        print(f"error: {f['file']}: {f['rule']}: {f['message']}")
    print(f"naming_validator: {len(findings)} finding(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
