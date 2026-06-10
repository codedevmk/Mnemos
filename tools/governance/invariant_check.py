#!/usr/bin/env python3
"""Gate G4: invariant coverage on accepted documents (MNE-CTX-PLAN-001 §7.1).

Every invariant declared in the front matter of an accepted L0/L1 document
must name at least one verifier in `verified_by`, and every named verifier
must exist in the repository (file or directory). Whether the verifier
*passes* is the concern of the gate that runs it (G1/G5/G6/CI); G4 pins the
mapping so no invariant silently dangles.

Coverage threshold is 100% on accepted documents. Exits 0/1.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
FRONT_MATTER_RE = re.compile(r"\A---\n(.*?\n)---\n", re.DOTALL)


def governed() -> list[Path]:
    docs = [REPO / "CONSTITUTION.md"]
    docs += sorted((REPO / "constitution").glob("*.md"))
    docs += sorted((REPO / "docs" / "adr" / "accepted").glob("*.md"))
    return [d for d in docs if d.exists()]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    errors: list[dict] = []
    declared = covered = 0

    for doc in governed():
        m = FRONT_MATTER_RE.match(doc.read_text(encoding="utf-8"))
        if not m:
            continue
        meta = yaml.safe_load(m.group(1))
        if not isinstance(meta, dict) or meta.get("status") != "accepted":
            continue
        for inv in meta.get("invariants") or []:
            declared += 1
            rel = doc.relative_to(REPO).as_posix()
            inv_id = inv.get("id", "?")
            verifiers = inv.get("verified_by") or []
            missing = [v for v in verifiers if not (REPO / v).exists()]
            if not verifiers:
                errors.append({"file": rel, "invariant": inv_id,
                               "message": "no verifier declared"})
            elif missing:
                errors.append({"file": rel, "invariant": inv_id,
                               "message": f"verifier(s) missing: {', '.join(missing)}"})
            else:
                covered += 1

    coverage = 1.0 if declared == 0 else covered / declared
    result = {"gate": "G4", "declared": declared, "covered": covered,
              "coverage": coverage, "errors": errors}
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(result, indent=2), encoding="utf-8")
    for e in errors:
        print(f"error: {e['file']}: {e['invariant']}: {e['message']}")
    print(f"invariant_check: {covered}/{declared} covered ({coverage:.0%})")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
