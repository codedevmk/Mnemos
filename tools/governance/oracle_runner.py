#!/usr/bin/env python3
"""Gates G5 (oracle ratchet) and G6 (golden determinism) — MNE-CTX-PLAN-001 §7.

Runs every suite in tests/oracles/registry.yaml through CTest and compares the
outcome against the high-water marks in tests/oracles/highwater.json.

Outcome per suite: passed | skipped (data-gated corpus absent, CTest "Not Run"
with SKIP_RETURN_CODE 4 honored as Skipped) | failed | not-built.

Ratchet rules (per suite, zero human involvement):
  - high-water "passed" + outcome failed                      -> violation
  - high-water "passed" + outcome skipped, suite not data-gated -> violation
  - high-water "passed" + outcome not-built                   -> violation
    (unless the suite is marked requires_apps and the build is headless)
  - outcome passed, high-water below                          -> raise
    (recorded only with --update; CI nightly commits raises)

Granularity is suite-level for now: the harnesses do not yet emit per-vector
JSON (ADR-0016). G6 = the registry's machine golden suites under the same
rules; a failed golden suite is a determinism divergence.

Exits 0/1. --build-dir is required; CI builds first.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
REGISTRY = REPO / "tests" / "oracles" / "registry.yaml"
HIGHWATER = REPO / "tests" / "oracles" / "highwater.json"
ORDER = {"never-run": 0, "skipped": 1, "passed": 2}


def load_suites() -> list[dict]:
    reg = yaml.safe_load(REGISTRY.read_text(encoding="utf-8"))
    suites: list[dict] = []
    for chip, spec in reg.get("chips", {}).items():
        for s in spec.get("suites", []):
            suites.append({**s, "owner": chip, "gate": "G5"})
    for machine, spec in reg.get("machines", {}).items():
        for s in spec.get("golden", []):
            suites.append({**s, "owner": machine, "gate": "G6"})
        for s in spec.get("parity", []):
            suites.append({**s, "owner": machine, "gate": "G5"})
    return suites


def run_suite(build_dir: Path, ctest_name: str) -> str:
    proc = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "-R", f"^{ctest_name}$"],
        capture_output=True,
        text=True,
    )
    out = proc.stdout + proc.stderr  # "No tests were found!!!" goes to stderr
    if re.search(r"No tests were found", out):
        return "not-built"
    if re.search(r"\*\*\*Skipped|100% tests passed.*\n.*Skipped", out) or "Skipped" in out:
        # CTest reports data-gated suites (SKIP_RETURN_CODE 4) as Skipped and
        # still exits 0; treat any skip marker as skipped before pass/fail.
        if proc.returncode == 0 and "***Failed" not in out:
            return "skipped"
    if proc.returncode == 0:
        return "passed"
    return "failed"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--update", action="store_true", help="record high-water raises")
    ap.add_argument("--headless", action="store_true",
                    help="build has MNEMOS_BUILD_APPS=OFF; requires_apps suites may be absent")
    ap.add_argument("--gate", choices=["G5", "G6"], help="restrict to one gate")
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    build_dir = Path(args.build_dir)
    highwater: dict = (
        json.loads(HIGHWATER.read_text(encoding="utf-8")) if HIGHWATER.exists() else {}
    )
    marks: dict = highwater.get("suites", {})

    violations: list[str] = []
    raises: list[str] = []
    rows: list[dict] = []

    for suite in load_suites():
        if args.gate and suite["gate"] != args.gate:
            continue
        sid, name = suite["id"], suite["ctest"]
        outcome = run_suite(build_dir, name)
        mark = marks.get(sid, {}).get("high_water", "never-run")

        if outcome == "not-built" and suite.get("requires_apps") and args.headless:
            verdict = "absent-ok"
        elif mark == "passed" and outcome == "failed":
            verdict = "RATCHET-VIOLATION"
            violations.append(f"{sid} ({name}): high-water passed, now failed")
        elif mark == "passed" and outcome == "skipped" and not suite.get("data_gated"):
            verdict = "RATCHET-VIOLATION"
            violations.append(f"{sid} ({name}): non-data-gated suite regressed to skipped")
        elif mark == "passed" and outcome == "not-built":
            verdict = "RATCHET-VIOLATION"
            violations.append(f"{sid} ({name}): suite disappeared from the build")
        elif outcome == "failed":
            verdict = "failed-below-floor"  # never passed yet: reported, not gating
        elif ORDER.get(outcome, 0) > ORDER.get(mark, 0):
            verdict = "raise"
            raises.append(sid)
            if args.update:
                marks[sid] = {"high_water": outcome, "ctest": name}
        else:
            verdict = "ok"
        rows.append({"id": sid, "ctest": name, "gate": suite["gate"],
                     "outcome": outcome, "high_water": mark, "verdict": verdict})
        print(f"{suite['gate']}  {outcome:9} hw={mark:9} {verdict:17} {sid}")

    if args.update:
        HIGHWATER.write_text(
            json.dumps({"schema": "mnemos-highwater/1", "suites": marks},
                       indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    if args.json_out:
        Path(args.json_out).write_text(
            json.dumps({"suites": rows, "violations": violations, "raises": raises},
                       indent=2), encoding="utf-8")

    for v in violations:
        print(f"error: {v}")
    print(f"oracle_runner: {len(violations)} violation(s), {len(raises)} raise(s)"
          + ("" if args.update or not raises else " (run with --update to record)"))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
