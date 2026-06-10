#!/usr/bin/env python3
"""Nightly metrics aggregator (MNE-CTX-PLAN-001 §10).

Computes the metric register from repository state, gate-tool output, and
session logs — no manual entry anywhere. Every metric is either measured or
emitted as null with the reason; nothing is synthesized.

Writes metrics/snapshots/YYYY-MM-DD.json and regenerates metrics/DASHBOARD.md.
Always exits 0 (the gates themselves block; metrics observe).

Sources per metric:
  M1  capsule drift            gen_capsule --check (pilot modules)
  M2  dead references          doc_liveness
  M3  invariant coverage       invariant_check
  M4  prose-only claim ratio   RFC-2119 keyword lines vs verified invariants
                               in constitution modules (trend-only, R2)
  M5  authority churn          git numstat over L0/L1, trailing 7 days
  M6  tokens/accepted change   null until OTel ingestion (D8) lands
  M7  context-escape rate      metrics/sessions/*.jsonl vs task-contract read
                               manifests; null until contracts declare them
  M8  first-pass acceptance    commits with no revert/fixup within 48h, 14d window
  M9  oracle state             tests/oracles/highwater.json (suite-level)
  M10 opcode vector coverage   null until harnesses emit per-vector JSON (ADR-0016)
  M11 determinism divergences  G6 suites at high-water failed (none may be)
  M12 decision latency         median proposed->ratified days, ADR front matter
  M13 proposal expiry rate     expired / (expired+proposed+recently ratified)
  M14 gate latency             null until CI duration ingestion lands
  M15 onboarding reads         session logs: reads before first edit
  M16 ratification actions     documents ratified in the trailing 7 days
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent
PILOT_MODULES = ["src/chips/cpu/z80", "src/manifests/sms"]
AUTHORITY_PATHS = ["CONSTITUTION.md", "constitution", "docs/adr/accepted"]
FRONT_MATTER_RE = re.compile(r"\A---\n(.*?\n)---\n", re.DOTALL)
RFC2119_RE = re.compile(r"\b(MUST NOT|MUST|SHALL|SHOULD NOT|SHOULD|MAY NOT|MAY)\b")


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(REPO), *args],
                          capture_output=True, text=True, check=True).stdout


def run_tool(script: str, *args: str) -> tuple[int, dict | None]:
    out = TOOLS / f".{script}.tmp.json"
    proc = subprocess.run(
        [sys.executable, str(TOOLS / script), *args, "--json", str(out)],
        capture_output=True, text=True, cwd=str(TOOLS),
    )
    data = json.loads(out.read_text(encoding="utf-8")) if out.exists() else None
    out.unlink(missing_ok=True)
    return proc.returncode, data


def front_matters() -> list[dict]:
    docs = [REPO / "CONSTITUTION.md"]
    docs += sorted((REPO / "constitution").glob("*.md"))
    for sub in ("proposed", "accepted", "superseded"):
        docs += sorted((REPO / "docs" / "adr" / sub).glob("*.md"))
    metas = []
    for d in docs:
        m = FRONT_MATTER_RE.match(d.read_text(encoding="utf-8"))
        if m:
            meta = yaml.safe_load(m.group(1))
            if isinstance(meta, dict):
                metas.append(meta)
    return metas


def m4_prose_ratio() -> dict:
    rfc_lines = 0
    verified = 0
    for doc in sorted((REPO / "constitution").glob("*.md")):
        if doc.name.startswith(("MNE-", "MIGRATION")):
            continue
        text = doc.read_text(encoding="utf-8")
        m = FRONT_MATTER_RE.match(text)
        body = text[m.end():] if m else text
        rfc_lines += sum(1 for line in body.splitlines() if RFC2119_RE.search(line))
        if m:
            meta = yaml.safe_load(m.group(1)) or {}
            verified += sum(1 for inv in (meta.get("invariants") or [])
                            if inv.get("verified_by"))
    ratio = None if rfc_lines == 0 else max(0.0, 1.0 - verified / rfc_lines)
    return {"rfc2119_lines": rfc_lines, "verified_invariants": verified,
            "ratio": round(ratio, 3) if ratio is not None else None,
            "note": "heuristic, trend-only (risk R2)"}


def m5_authority_churn(today: dt.date) -> int:
    since = (today - dt.timedelta(days=7)).isoformat()
    out = git("log", f"--since={since}", "--numstat", "--format=", "--",
              *AUTHORITY_PATHS)
    return sum(
        int(a) + int(b)
        for a, b, *_ in (line.split("\t") for line in out.splitlines() if line)
        if a != "-" and b != "-"
    )


def m8_first_pass(today: dt.date) -> dict:
    since = (today - dt.timedelta(days=14)).isoformat()
    subjects = [s for s in git("log", f"--since={since}", "--format=%s").splitlines() if s]
    if not subjects:
        return {"total": 0, "clean": 0, "rate": None}
    dirty = sum(1 for s in subjects
                if s.startswith(("Revert ", "fixup!", "squash!")))
    total = len(subjects)
    return {"total": total, "clean": total - dirty,
            "rate": round((total - dirty) / total, 3)}


def m9_m11_oracles() -> tuple[dict, int | None]:
    hw_path = REPO / "tests" / "oracles" / "highwater.json"
    if not hw_path.exists():
        return {"note": "highwater.json absent"}, None
    suites = json.loads(hw_path.read_text(encoding="utf-8")).get("suites", {})
    states: dict[str, int] = {}
    for spec in suites.values():
        states[spec["high_water"]] = states.get(spec["high_water"], 0) + 1
    reg = yaml.safe_load((REPO / "tests" / "oracles" / "registry.yaml")
                         .read_text(encoding="utf-8"))
    golden_ids = {s["id"] for m in reg.get("machines", {}).values()
                  for s in m.get("golden", [])}
    divergences = sum(1 for sid, spec in suites.items()
                      if sid in golden_ids and spec["high_water"] == "failed")
    return {"suite_high_water": states, "total_suites": len(suites)}, divergences


def m12_m13_m16(today: dt.date, metas: list[dict]) -> tuple[float | None, dict, int]:
    latencies = []
    expired = proposed = ratified_total = 0
    ratified_this_week = 0
    week_ago = today - dt.timedelta(days=7)
    for meta in metas:
        status = meta.get("status")
        p, r = meta.get("proposed"), meta.get("ratified")
        if status == "accepted" and p and r:
            latencies.append((dt.date.fromisoformat(str(r))
                              - dt.date.fromisoformat(str(p))).days)
        if status == "accepted" and r:
            ratified_total += 1
            if dt.date.fromisoformat(str(r)) > week_ago:
                ratified_this_week += 1
        elif status == "proposed":
            proposed += 1
        elif status == "expired":
            expired += 1
    latency = statistics.median(latencies) if latencies else None
    denom = expired + proposed + ratified_total
    expiry = {"expired": expired, "open_proposals": proposed,
              "rate": round(expired / denom, 3) if denom else None}
    return latency, expiry, ratified_this_week


def session_metrics() -> dict:
    sessions_dir = REPO / "metrics" / "sessions"
    logs = sorted(sessions_dir.glob("*.jsonl")) if sessions_dir.is_dir() else []
    if not logs:
        return {"sessions_logged": 0, "M7": None, "M15": None,
                "capture_omissions": 0,
                "note": "no session logs; hooks install via .claude/settings.json"}
    onboarding: list[int] = []
    omissions = 0
    for log in logs:
        reads_before_edit = 0
        seen_edit = False
        for line in log.read_text(encoding="utf-8").splitlines():
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue
            if ev.get("event") == "read" and not seen_edit:
                reads_before_edit += 1
            elif ev.get("event") == "edit":
                seen_edit = True
            elif ev.get("event") == "capture_omission":
                omissions += 1
        if seen_edit:
            onboarding.append(reads_before_edit)
    return {
        "sessions_logged": len(logs),
        "M7": None,  # needs task-contract read manifests to score against
        "M15": statistics.median(onboarding) if onboarding else None,
        "capture_omissions": omissions,
    }


def collect(today: dt.date) -> dict:
    rc1, drift = run_tool("gen_capsule.py", "--check", *PILOT_MODULES)
    _, live = run_tool("doc_liveness.py")
    _, inv = run_tool("invariant_check.py")
    _, budget = run_tool("token_budget.py")
    oracles, divergences = m9_m11_oracles()
    metas = front_matters()
    latency, expiry, ratified_week = m12_m13_m16(today, metas)
    sess = session_metrics()

    return {
        "snapshot": today.isoformat(),
        "commit": git("rev-parse", "HEAD").strip(),
        "M1_capsule_drift": sum(
            1 for c in (drift or {}).get("capsules", [])
            if c["status"].startswith(("drift", "missing"))),
        "M2_dead_references": len((live or {}).get("dead_references", [])),
        "M3_invariant_coverage": (inv or {}).get("coverage"),
        "M4_prose_only_claims": m4_prose_ratio(),
        "M5_authority_churn_lines_7d": m5_authority_churn(today),
        "M6_tokens_per_change": None,
        "M6_note": "pending OTel ingestion (D8)",
        "M7_context_escape": sess["M7"],
        "M8_first_pass": m8_first_pass(today),
        "M9_oracle_state": oracles,
        "M10_opcode_coverage": None,
        "M10_note": "pending per-vector harness JSON (ADR-0016)",
        "M11_determinism_divergences": divergences,
        "M12_decision_latency_days": latency,
        "M13_proposal_expiry": expiry,
        "M14_gate_latency": None,
        "M14_note": "pending CI duration ingestion",
        "M15_onboarding_reads_median": sess["M15"],
        "M16_ratifications_7d": ratified_week,
        "sessions": {k: sess[k] for k in ("sessions_logged", "capture_omissions")},
        "G11_token_budget": {
            "breaches": (budget or {}).get("breaches"),
            "files": (budget or {}).get("files"),
        },
    }


def render_dashboard(snap: dict) -> str:
    def fmt(v):
        return "—" if v is None else v
    m8 = snap["M8_first_pass"]
    m13 = snap["M13_proposal_expiry"]
    rows = [
        ("M1 capsule drift", snap["M1_capsule_drift"], "0 (G2)"),
        ("M2 dead references", snap["M2_dead_references"], "0 (G3)"),
        ("M3 invariant coverage", snap["M3_invariant_coverage"], "1.0 (G4)"),
        ("M4 prose-only claim ratio", snap["M4_prose_only_claims"]["ratio"], "trend ↓"),
        ("M5 authority churn (lines/7d)", snap["M5_authority_churn_lines_7d"], "trend → 0"),
        ("M6 tokens / accepted change", fmt(snap["M6_tokens_per_change"]), "−40% vs wk0 [H1]"),
        ("M7 context-escape rate", fmt(snap["M7_context_escape"]), "< 15% [H2]"),
        ("M8 first-pass acceptance", fmt(m8["rate"]), "≥ 80% [H3]"),
        ("M11 determinism divergences", fmt(snap["M11_determinism_divergences"]), "0 (G6)"),
        ("M12 decision latency (days)", fmt(snap["M12_decision_latency_days"]), "trend"),
        ("M13 proposal expiry rate", fmt(m13["rate"]), "trend"),
        ("M14 gate latency", fmt(snap["M14_gate_latency"]), "≤ 5 min PR suite"),
        ("M15 onboarding reads (median)", fmt(snap["M15_onboarding_reads_median"]), "−50% vs wk0 [H4]"),
        ("M16 ratifications (7d)", snap["M16_ratifications_7d"], "≤ 6/week"),
    ]
    hw = snap["M9_oracle_state"].get("suite_high_water", {})
    lines = [
        "# Metrics Dashboard",
        "",
        f"Generated by `tools/governance/metrics.py` from snapshot "
        f"`{snap['snapshot']}` at `{snap['commit'][:12]}`. Do not edit.",
        "",
        "| Metric | Value | Target |",
        "|--------|-------|--------|",
    ]
    lines += [f"| {n} | {v} | {t} |" for n, v, t in rows]
    lines += [
        "",
        "Oracle high-water (M9, suite-level): "
        + (", ".join(f"{k}: {v}" for k, v in sorted(hw.items())) or "uninitialized")
        + f" — {snap['M9_oracle_state'].get('total_suites', 0)} suites registered.",
        "",
        "Null values are unmeasured, never estimated: M6 awaits OTel ingestion "
        "(D8), M7 awaits task-contract read manifests, M10 awaits per-vector "
        "harness output, M14 awaits CI duration ingestion.",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--today", help="override date (YYYY-MM-DD) for reproducible runs")
    args = ap.parse_args()
    today = dt.date.fromisoformat(args.today) if args.today else dt.date.today()

    snap = collect(today)
    out_dir = REPO / "metrics" / "snapshots"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{today.isoformat()}.json").write_text(
        json.dumps(snap, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (REPO / "metrics" / "DASHBOARD.md").write_text(
        render_dashboard(snap), encoding="utf-8")
    print(f"metrics: snapshot {today.isoformat()} written; dashboard regenerated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
