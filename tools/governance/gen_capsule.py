#!/usr/bin/env python3
"""Gate G2: generate / drift-check subsystem capsules (MNE-CTX-PLAN-001 §6).

A CAPSULE.md is a deterministic assembly of five blocks — one human, four
extracted. Nothing here is summarized: generated blocks are mechanical
extraction only (L0 standing rule 5).

  Intent        — first paragraph of the module README.md (<= 150 words,
                  human-ratified), included verbatim
  Public API    — extract_symbols.py over the module's root headers
  Dependencies  — target_link_libraries entries in the module CMakeLists +
                  cross-module include edges
  Tests         — module tests/ inventory + repo test files carrying the
                  module's leaf name; oracle registry status (factual)
  Freshness     — front matter: source content digest, generator version,
                  intent digest, token estimate

Determinism: every field derives from tracked file *content* (sha256 digests),
never from commit SHAs or timestamps, so regeneration on an unchanged module
is byte-identical and `--check` is a pure byte comparison. Exits 0/1.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from extract_symbols import parse_header
from token_budget import estimate_tokens

REPO = Path(__file__).resolve().parents[2]
GENERATOR = "gen_capsule/0.1"
INTENT_WORD_LIMIT = 150
CAPSULE_TOKEN_BUDGET = 6000


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def tracked(module: Path) -> list[Path]:
    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", "-z", "--", module.relative_to(REPO).as_posix()],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [REPO / p for p in out.split("\0") if p and not p.endswith("CAPSULE.md")]


def intent_fragment(module: Path) -> tuple[str, str]:
    """First paragraph of README.md after the title; (text, sha256)."""
    readme = module / "README.md"
    if not readme.exists():
        raise SystemExit(f"error: {readme.relative_to(REPO)} missing — capsules require an intent fragment")
    lines = readme.read_text(encoding="utf-8").splitlines()
    para: list[str] = []
    for line in lines:
        if line.startswith("#"):
            continue
        if not line.strip():
            if para:
                break
            continue
        para.append(line)
    text = "\n".join(para).strip()
    words = len(text.split())
    if not text or words > INTENT_WORD_LIMIT:
        raise SystemExit(
            f"error: intent fragment in {readme.relative_to(REPO)} is "
            f"{words} words (limit {INTENT_WORD_LIMIT}, minimum 1)"
        )
    return text, sha(text.encode("utf-8"))


def api_block(module: Path) -> str:
    rows: list[str] = []
    for header in sorted(module.glob("*.hpp")) + sorted(module.glob("*.h")):
        parsed = parse_header(header)
        rows.append(f"### `{header.name}`")
        if parsed["namespaces"]:
            rows.append("namespaces: " + ", ".join(f"`{n}`" for n in parsed["namespaces"]))
        for t in parsed["types"]:
            rows.append(f"- {t['kind']} `{t['name']}`")
            if t["public_methods"]:
                rows.append("  - public: " + ", ".join(f"`{m}`" for m in t["public_methods"]))
        for u in parsed["usings"]:
            rows.append(f"- using `{u}`")
        for fn in parsed["functions"]:
            rows.append(f"- function `{fn}`")
        rows.append("")
    return "\n".join(rows).rstrip()


def owner_module(header_path: Path) -> str:
    return header_path.parent.relative_to(REPO).as_posix()


def dependency_block(module: Path) -> str:
    deps: set[str] = set()
    cml = module / "CMakeLists.txt"
    if cml.exists():
        text = re.sub(r"#[^\n]*", "", cml.read_text(encoding="utf-8"))
        for m in re.finditer(r"target_link_libraries\s*\(([^)]*)\)", text, re.DOTALL):
            for tok in m.group(1).split():
                if "::" in tok:
                    deps.add(tok)

    header_owner: dict[str, Path] = {}
    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", "-z", "*.hpp", "*.h"],
        capture_output=True, text=True, check=True,
    ).stdout
    for p in out.split("\0"):
        if p:
            header_owner[Path(p).name] = REPO / p

    # Product and test include edges are reported separately: a test may link
    # higher tiers (mock buses, harnesses) without implying a tier violation
    # in the library itself (ARCH-001).
    product_edges: set[str] = set()
    test_edges: set[str] = set()
    for src in tracked(module):
        if src.suffix not in {".hpp", ".h", ".cpp", ".cc"}:
            continue
        sink = test_edges if "tests" in src.relative_to(module).parts else product_edges
        for m in re.finditer(r'#include\s+"([^"]+)"', src.read_text(encoding="utf-8", errors="replace")):
            owner = header_owner.get(Path(m.group(1)).name)
            if owner is not None and not owner.is_relative_to(module):
                sink.add(f"{m.group(1)} <- {owner_module(owner)}")

    lines = ["link targets:"] + [f"- `{d}`" for d in sorted(deps)]
    lines += ["", "product include edges (header <- owning module):"]
    lines += [f"- `{e}`" for e in sorted(product_edges)] or ["- (none)"]
    lines += ["", "test-only include edges:"]
    lines += [f"- `{e}`" for e in sorted(test_edges - product_edges)] or ["- (none)"]
    return "\n".join(lines)


def test_block(module: Path, leaf: str) -> str:
    lines = ["module tests:"]
    tests_dir = module / "tests"
    entries = sorted(p.name for p in tests_dir.glob("*.cpp")) if tests_dir.is_dir() else []
    lines += [f"- `{e}`" for e in entries] or ["- (none)"]
    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", "-z", "tests/"],
        capture_output=True, text=True, check=True,
    ).stdout
    repo_tests = sorted(
        p for p in out.split("\0")
        if p.endswith(".cpp") and re.search(rf"(^|_){re.escape(leaf)}(_|\.|$)", Path(p).stem)
    )
    lines += ["", "repository tests referencing this module (leaf-name match):"]
    lines += [f"- `{t}`" for t in repo_tests] or ["- (none)"]
    registry = REPO / "tests" / "oracles" / "registry.yaml"
    lines += ["", "oracle registry: "
              + ("`tests/oracles/registry.yaml`" if registry.exists()
                 else "not yet populated (pilot phase P2)")]
    return "\n".join(lines)


def build_capsule(module: Path) -> str:
    rel = module.relative_to(REPO).as_posix()
    leaf = module.name if module.name != "sms" else "sms"
    intent, intent_sha = intent_fragment(module)

    digest_input = "".join(
        # CRLF-normalize so the digest derives from the canonical (committed)
        # content: with core.autocrlf the same tracked text file has different
        # working-tree bytes on Windows vs Linux, and a capsule generated on one
        # platform would permanently "drift" on the other.
        f"{p.relative_to(REPO).as_posix()}:{sha(p.read_bytes().replace(b'\r\n', b'\n'))}\n"
        for p in sorted(tracked(module))
    )
    source_digest = sha(digest_input.encode("utf-8"))

    body = "\n".join([
        f"# Capsule — `{rel}`",
        "",
        "Generated by `tools/governance/gen_capsule.py`. Never hand-edited;",
        "drift fails gate G2. Blocks are mechanical extraction; the intent",
        "block is the human-ratified first paragraph of `README.md`.",
        "",
        "## Intent",
        "",
        intent,
        "",
        "## Public API",
        "",
        api_block(module),
        "",
        "## Dependencies",
        "",
        dependency_block(module),
        "",
        "## Tests & oracle status",
        "",
        test_block(module, leaf),
        "",
    ])

    front = "\n".join([
        "---",
        f"capsule: {rel}",
        f"generator: {GENERATOR}",
        f"source_digest: sha256:{source_digest}",
        f"intent_ratified: sha256:{intent_sha}",
        f"token_estimate: {estimate_tokens(body)}",
        "---",
        "",
    ])
    return front + body


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("modules", nargs="+", help="module directories (repo-relative)")
    ap.add_argument("--check", action="store_true", help="fail on drift instead of writing")
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    findings: list[dict] = []
    for raw in args.modules:
        module = REPO / raw
        if not module.is_dir():
            findings.append({"module": raw, "status": "missing-module"})
            continue
        content = build_capsule(module)
        tokens = estimate_tokens(content)
        capsule = module / "CAPSULE.md"
        if args.check:
            current = capsule.read_text(encoding="utf-8") if capsule.exists() else ""
            status = "ok" if current == content else "drift"
        else:
            capsule.write_text(content, encoding="utf-8")
            status = "written"
        if tokens > CAPSULE_TOKEN_BUDGET:
            status += "+over-budget"  # budget breach is advisory (G11), reported here
        findings.append({"module": raw, "status": status, "tokens": tokens})
        print(f"{status:14} {tokens:>5} tok  {raw}")

    if args.json_out:
        Path(args.json_out).write_text(
            json.dumps({"gate": "G2", "capsules": findings}, indent=2), encoding="utf-8"
        )
    drift = [f for f in findings if f["status"].startswith(("drift", "missing"))]
    print(f"gen_capsule: {len(drift)} drift/missing finding(s)")
    return 1 if drift else 0


if __name__ == "__main__":
    sys.exit(main())
