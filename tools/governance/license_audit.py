#!/usr/bin/env python3
"""Gate G7: license audit (ARCH-005).

Mnemos declares licenses at tree level (ADR-0003), not per file, so the gate
checks:
  - LICENSE is Apache-2.0 and LICENSE-chips is MIT
  - no code file declares a copyleft license (denylist scan)
  - every FetchContent dependency is enumerated in THIRD_PARTY_NOTICES.md

The denylist scan covers code and build files only; prose mentions of GPL in
markdown (policy text, provenance notes) are not license declarations.

Deterministic; exits 0 on zero violations, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

CODE_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".cxx", ".cmake", ".py", ".capnp", ".txt"}
DENYLIST = [
    re.compile(r"SPDX-License-Identifier:\s*(GPL|LGPL|AGPL)", re.IGNORECASE),
    re.compile(r"GNU (General|Lesser General|Affero General) Public License"),
]


def tracked_files() -> list[Path]:
    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", "-z"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [REPO / p for p in out.split("\0") if p]


def fetchcontent_deps() -> set[str]:
    deps: set[str] = set()
    decl = re.compile(r"FetchContent_Declare\(\s*([A-Za-z0-9_-]+)", re.MULTILINE)
    for path in tracked_files():
        if path.suffix == ".cmake" or path.name == "CMakeLists.txt":
            deps.update(decl.findall(path.read_text(encoding="utf-8", errors="replace")))
    return deps


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    violations: list[dict] = []

    def bad(kind: str, where: str, msg: str) -> None:
        violations.append({"kind": kind, "file": where, "message": msg})

    license_core = REPO / "LICENSE"
    license_chips = REPO / "LICENSE-chips"
    if not license_core.exists() or "Apache License" not in license_core.read_text(
        encoding="utf-8"
    ):
        bad("tree-license", "LICENSE", "missing or not Apache-2.0")
    if not license_chips.exists() or "MIT License" not in license_chips.read_text(
        encoding="utf-8"
    ):
        bad("tree-license", "LICENSE-chips", "missing or not MIT")

    for path in tracked_files():
        if path.suffix not in CODE_SUFFIXES:
            continue
        rel = path.relative_to(REPO).as_posix()
        if not rel.startswith(("src/", "tests/", "tools/", "cmake/", "extern/")):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern in DENYLIST:
            if pattern.search(text):
                bad("copyleft", rel, f"matches denylist pattern `{pattern.pattern}`")
                break

    def canon(name: str) -> str:
        # FetchContent names and notices spellings differ in separators only
        # (nlohmann_json vs. nlohmann/json), so compare separator-free.
        return re.sub(r"[-_/. ]", "", name.lower())

    notices_path = REPO / "THIRD_PARTY_NOTICES.md"
    notices = (
        canon(notices_path.read_text(encoding="utf-8"))
        if notices_path.exists()
        else ""
    )
    for dep in sorted(fetchcontent_deps()):
        if canon(dep) not in notices:
            bad(
                "undocumented-dependency",
                "THIRD_PARTY_NOTICES.md",
                f"FetchContent dependency `{dep}` is not enumerated",
            )

    result = {"gate": "G7", "violations": violations}
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(result, indent=2), encoding="utf-8")
    for v in violations:
        print(f"error: {v['file']}: {v['kind']}: {v['message']}")
    print(f"license_audit: {len(violations)} violation(s)")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
