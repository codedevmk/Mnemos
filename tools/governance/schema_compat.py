#!/usr/bin/env python3
"""Gate G8: Cap'n Proto schema compatibility (ARCH-002, MNE-CTX-PLAN-001 §7.4).

Released schemas evolve append-only. The release procedure is: copy the
schema, as shipped, into `src/debug/wire/released/<name>.capnp`. This tool
structurally diffs every current schema in `src/debug/wire/` against its
released snapshot and fails on:

  - a released field removed
  - a released field renumbered (@N changed)
  - a released field's type changed
  - a released struct/enum/interface removed

New fields, structs, and enums are always allowed. With no released
snapshots (the current state — no .capnp has shipped yet) the gate is
trivially green. The structural parser covers the declaration grammar
(`name @N :Type;` inside struct/enum/interface blocks); `capnp compile`
validation can be layered on once the capnp toolchain is in CI.

Exits 0/1.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WIRE = REPO / "src" / "debug" / "wire"
RELEASED = WIRE / "released"

DECL_RE = re.compile(r"^\s*(struct|enum|interface)\s+(\w+)")
FIELD_RE = re.compile(r"^\s*(\w+)\s*@(\d+)\s*(?::\s*([^;=]+))?[;=]")


def parse_schema(path: Path) -> dict:
    """{container: {field: (ordinal, type)}} — comment-stripped, structural."""
    text = re.sub(r"#[^\n]*", "", path.read_text(encoding="utf-8"))
    containers: dict[str, dict] = {}
    stack: list[str] = []
    for line in text.splitlines():
        d = DECL_RE.match(line)
        if d:
            stack.append(d.group(2))
            containers.setdefault(".".join(stack), {})
        f = FIELD_RE.match(line)
        if f and stack:
            name, ordinal, ftype = f.group(1), int(f.group(2)), (f.group(3) or "").strip()
            containers[".".join(stack)][name] = (ordinal, ftype)
        # brace bookkeeping: declarations close with '}' on its own depth
        stack_depth_delta = line.count("{") - line.count("}")
        for _ in range(-stack_depth_delta):
            if stack:
                stack.pop()
    return containers


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", dest="json_out")
    args = ap.parse_args()

    breaks: list[str] = []
    checked = 0
    if RELEASED.is_dir():
        for released_schema in sorted(RELEASED.glob("*.capnp")):
            current_schema = WIRE / released_schema.name
            checked += 1
            if not current_schema.exists():
                breaks.append(f"{released_schema.name}: released schema removed")
                continue
            released = parse_schema(released_schema)
            current = parse_schema(current_schema)
            for container, fields in released.items():
                if container not in current:
                    breaks.append(f"{released_schema.name}: `{container}` removed")
                    continue
                for fname, (ordinal, ftype) in fields.items():
                    cur = current[container].get(fname)
                    if cur is None:
                        breaks.append(
                            f"{released_schema.name}: `{container}.{fname}` removed")
                    elif cur[0] != ordinal:
                        breaks.append(
                            f"{released_schema.name}: `{container}.{fname}` renumbered "
                            f"@{ordinal} -> @{cur[0]}")
                    elif ftype and cur[1] != ftype:
                        breaks.append(
                            f"{released_schema.name}: `{container}.{fname}` type changed "
                            f"`{ftype}` -> `{cur[1]}`")

    if args.json_out:
        Path(args.json_out).write_text(
            json.dumps({"gate": "G8", "released_checked": checked, "breaks": breaks},
                       indent=2), encoding="utf-8")
    for b in breaks:
        print(f"error: {b}")
    print(f"schema_compat: {checked} released schema(s) checked, {len(breaks)} break(s)")
    return 1 if breaks else 0


if __name__ == "__main__":
    sys.exit(main())
