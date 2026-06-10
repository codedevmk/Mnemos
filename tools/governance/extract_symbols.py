#!/usr/bin/env python3
"""Symbol extraction over module headers (feeds G2 capsules and G3 liveness).

Engine: a restricted declaration-grammar parser over clang-formatted headers
(the R1 fallback from MNE-CTX-PLAN-001 D6, adopted first per ADR-0015 —
deterministic everywhere, no libclang dependency; promotion to libclang is a
proposed follow-up). It extracts, per header:

  - namespaces
  - class / struct / enum names declared at namespace scope
  - public member function names declared directly in a class body
  - using-aliases at namespace scope

Output is sorted, deterministic JSON. `--all` scans every tracked header and
emits the flat repository symbol inventory used by doc_liveness.py.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

KEYWORDS = {
    "if", "for", "while", "switch", "return", "sizeof", "static_assert",
    "alignas", "alignof", "decltype", "noexcept", "catch", "throw", "assert",
    "defined", "new", "delete", "co_await", "co_return", "co_yield", "requires",
}

NS_RE = re.compile(r"^(?:inline\s+)?namespace\s+([\w:]+)\s*\{")
TYPE_RE = re.compile(
    r"^(?:template\s*<[^>]*>\s*)?(class|struct|enum\s+class|enum)\s+(\w+)"
)
ACCESS_RE = re.compile(r"^(public|protected|private)\s*:")
METHOD_RE = re.compile(
    r"^(?:\[\[[^\]]*\]\]\s*)?(?:template\s*<[^>]*>\s*)?"
    r"(?:(?:virtual|static|constexpr|explicit|inline|friend)\s+)*"
    r"[\w:<>,&*\s]*?(~?\w+|operator\S*)\s*\("
)
USING_RE = re.compile(r"^using\s+(\w+)\s*=")
# Declarations may wrap across lines, so no terminator is required; at
# namespace scope in a header the only call-shaped lines are declarations.
FREE_FN_RE = re.compile(
    r"^(?:\[\[[^\]]*\]\]\s*)?(?:template\s*<[^>]*>\s*)?"
    r"(?:(?:inline|constexpr|static)\s+)*"
    r"[\w:<>,&*\s]+\s[&*]?(\w+)\s*\("
)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)
    text = re.sub(r"'(?:[^'\\]|\\.)*'", "''", text)
    return text


def parse_header(path: Path) -> dict:
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    namespaces: set[str] = set()
    types: dict[str, dict] = {}
    usings: set[str] = set()
    functions: set[str] = set()

    depth = 0
    # Open scopes: [kind, name, access, depth-at-which-the-scope's-brace-opened]
    scopes: list[list] = []

    for raw_line in text.splitlines():
        line = raw_line.strip()
        top = scopes[-1] if scopes else None
        directly_in = top is not None and depth == top[3]
        at_ns_scope = top is None or (top[0] == "ns" and directly_in)
        in_type_body = top is not None and top[0] == "type" and directly_in

        pending = None  # scope to bind to the next '{'

        m = NS_RE.match(line)
        if m:
            namespaces.add(m.group(1))
            pending = ["ns", m.group(1), None]
        elif at_ns_scope:
            m = TYPE_RE.match(line)
            if m and not line.endswith(";"):
                kind = re.sub(r"\s+", " ", m.group(1))
                name = m.group(2)
                types[name] = {"kind": kind, "name": name, "public_methods": []}
                access = "private" if kind == "class" else "public"
                pending = ["type", name, access]
            else:
                u = USING_RE.match(line)
                if u:
                    usings.add(u.group(1))
                else:
                    ff = FREE_FN_RE.match(line)
                    if ff and ff.group(1) not in KEYWORDS and not line.startswith("#"):
                        functions.add(ff.group(1))
        elif in_type_body:
            a = ACCESS_RE.match(line)
            if a:
                top[2] = a.group(1)
            elif top[2] == "public" and "(" in line and not line.startswith("#"):
                f = METHOD_RE.match(line)
                if f and f.group(1) not in KEYWORDS:
                    holder = types.get(top[1])
                    if holder is not None and f.group(1) not in holder["public_methods"]:
                        holder["public_methods"].append(f.group(1))

        for ch in raw_line:
            if ch == "{":
                depth += 1
                if pending is not None:
                    scopes.append(pending + [depth])
                    pending = None
            elif ch == "}":
                if scopes and scopes[-1][3] == depth:
                    scopes.pop()
                depth = max(0, depth - 1)

    return {
        "file": path.relative_to(REPO).as_posix(),
        "namespaces": sorted(namespaces),
        "types": [types[k] for k in sorted(types)],
        "usings": sorted(usings),
        "functions": sorted(functions - set(types)),
    }


def all_headers() -> list[Path]:
    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", "-z", "*.hpp", "*.h"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [REPO / p for p in out.split("\0") if p]


def inventory() -> list[str]:
    names: set[str] = set()
    for h in all_headers():
        parsed = parse_header(h)
        names.update(parsed["usings"])
        names.update(parsed["functions"])
        for t in parsed["types"]:
            names.add(t["name"])
            names.update(m.lstrip("~") for m in t["public_methods"])
        for ns in parsed["namespaces"]:
            names.update(ns.split("::"))
        names.add(Path(parsed["file"]).name)
    return sorted(names)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="*", help="header files or module directories")
    ap.add_argument("--all", action="store_true", help="emit repo symbol inventory")
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    if args.all:
        result: object = {"inventory": inventory()}
    else:
        headers: list[Path] = []
        for raw in args.paths:
            p = (REPO / raw) if not Path(raw).is_absolute() else Path(raw)
            if p.is_dir():
                headers.extend(sorted(p.glob("*.hpp")) + sorted(p.glob("*.h")))
            else:
                headers.append(p)
        result = {"modules": [parse_header(h) for h in headers]}

    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        Path(args.json_out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
