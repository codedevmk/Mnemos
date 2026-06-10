#!/usr/bin/env python3
"""Gate G3: dead-reference check on authority documents.

Every backtick-quoted *identifier* in scoped documents must resolve. P1 scope
(ADR-0015): `CONSTITUTION.md`, `constitution/*.md`, and the intent fragments
of capsuled modules — the documents agents load unconditionally. Accepted ADR
bodies join the scope in P2.

A span is an identifier candidate only if it has no whitespace and no
code-snippet punctuation (parentheses, quotes, `#`, `$`); other spans are
prose snippets, not references. A candidate resolves if it is any of:

  - an existing repository path (relative to repo root or the document),
    including glob patterns and `path:line` anchors
  - a governed-document / gate / metric / decision ID
    (ADR-0042, ARCH-001, INV-TIM-002, G5, M7, H1, D3, P0, R2, A1, ...)
  - a symbol in the extracted header inventory (extract_symbols.py --all)
  - a CMake target or namespace under `mnemos::`, a `std::` name, a
    compiler/tool flag (leading `-` or `/`), or a dotted chip/manifest ID
  - a document-lifecycle keyword (proposed, accepted, superseded, expired,
    rejected, axis-conflict)
  - escaped: the line contains `\\nolint` or `<!-- nolint -->`

Exits 0 on zero dead references, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from extract_symbols import inventory

REPO = Path(__file__).resolve().parents[2]

ID_RE = re.compile(
    r"^(ADR-\d{4}|ARCH-\d{3}|STD-\d{3}|INV-[A-Z]+-\d{3}|MNE-[A-Z0-9-]+|"
    r"ORC-[A-Z0-9-]+|[GMHDPR]\d{1,2}|A\d{1,2})$"
)
LIFECYCLE = {
    "proposed", "accepted", "superseded", "expired", "rejected", "axis-conflict",
    "ratified", "snake_case",
}
# Language / build-system vocabulary that is referenced normatively but is not
# a repository symbol.
VOCABULARY = {
    "constexpr", "consteval", "noexcept", "nullptr",
    "target_link_libraries", "FetchContent", "add_library", "ctest", "cmake",
    "build/", "scratch/",
}
EXTERNAL_API_PREFIXES = ("SDL_", "Vk", "vk", "capnp")
CANDIDATE_RE = re.compile(r"^[A-Za-z0-9_:.\-/*\\]+$")
SPAN_RE = re.compile(r"`([^`]+)`")


def capsuled_module_readmes() -> list[Path]:
    return [c.parent / "README.md" for c in REPO.rglob("CAPSULE.md")]


def scoped_documents() -> list[Path]:
    docs = []
    if (REPO / "CONSTITUTION.md").exists():
        docs.append(REPO / "CONSTITUTION.md")
    # MNE-* plan documents are exempt: their bodies are kept verbatim and
    # §3-style layouts name idealized/future paths by design (ADR-0015).
    docs.extend(
        p for p in sorted((REPO / "constitution").glob("*.md"))
        if not p.name.startswith(("MNE-", "MIGRATION"))
    )
    docs.extend(r for r in sorted(capsuled_module_readmes()) if r.exists())
    return docs


def resolves(span: str, doc: Path, symbols: set[str]) -> bool:
    if span in LIFECYCLE or span in VOCABULARY or span in symbols:
        return True
    if ID_RE.match(span):
        return True
    if span.startswith(("-", "/", ".", "std::", "mnemos::", "\\")):
        return True
    if span.startswith(EXTERNAL_API_PREFIXES):
        return True
    # CMake helper / target convention and env-var convention.
    if re.match(r"^mnemos_\w+$", span) or re.match(r"^MNEMOS_[A-Z0-9_]+$", span):
        return True
    # Scoped symbol: resolve on its leading component (sms_config::region).
    if "::" in span and span.split("::", 1)[0] in symbols:
        return True
    # Bare file name: resolve anywhere under the document's module.
    if "/" not in span and "." in span:
        if next(iter(doc.parent.rglob(span)), None) is not None:
            return True
    # Leading-wildcard patterns are depth-agnostic (`*/CAPSULE.md`).
    if span.startswith("*/"):
        try:
            if next(iter(REPO.glob("**/" + span[2:])), None) is not None:
                return True
        except (ValueError, NotImplementedError):
            pass
    bare = span.split(":")[0] if re.search(r":\d+$", span) else span
    for base in (REPO, doc.parent):
        if (base / bare).exists():
            return True
        if any(ch in bare for ch in "*?["):
            try:
                if next(iter(base.glob(bare)), None) is not None:
                    return True
            except (ValueError, NotImplementedError):
                pass
    if re.match(r"^[a-z0-9_]+(\.[a-z0-9_]+)+$", span):  # chip/manifest dotted IDs
        return span.rsplit(".", 1)[-1] not in {"md", "py", "cpp", "hpp", "yml", "yaml", "json", "cmake", "capnp", "toml"}
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    symbols = set(inventory())
    findings: list[dict] = []
    for doc in scoped_documents():
        in_code_block = False
        # For module READMEs only the intent fragment (first paragraph) is in
        # scope — it is what the capsule carries (MNE-CTX-PLAN-001 §8).
        lines = doc.read_text(encoding="utf-8").splitlines()
        if doc.name == "README.md":
            intent: list[tuple[int, str]] = []
            for n, line in enumerate(lines, 1):
                if line.startswith("#"):
                    continue
                if not line.strip():
                    if intent:
                        break
                    continue
                intent.append((n, line))
            numbered = intent
        else:
            numbered = list(enumerate(lines, 1))
        for n, line in numbered:
            if line.lstrip().startswith("```"):
                in_code_block = not in_code_block
                continue
            if in_code_block or "nolint" in line:
                continue
            for span in SPAN_RE.findall(line):
                if not CANDIDATE_RE.match(span):
                    continue  # prose snippet, not an identifier
                if not resolves(span, doc, symbols):
                    findings.append(
                        {
                            "file": doc.relative_to(REPO).as_posix(),
                            "line": n,
                            "reference": span,
                        }
                    )

    if args.json_out:
        Path(args.json_out).write_text(
            json.dumps({"gate": "G3", "dead_references": findings}, indent=2),
            encoding="utf-8",
        )
    for f in findings:
        print(f"error: {f['file']}:{f['line']}: dead reference `{f['reference']}`")
    print(f"doc_liveness: {len(findings)} dead reference(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
