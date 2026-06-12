#!/usr/bin/env python3
"""Repo-hygiene structure linter — the single enforcement source for ADR-0025.

Validates file placement against the repository hygiene rules (1-7). One ruleset
(config/repo-hygiene.toml) feeds three triggers: the Claude PreToolUse write hook,
the git pre-commit hook, and the CI gate.

    repo_hygiene.py --path <file>   check one path           (Claude write hook)
    repo_hygiene.py --staged        check staged files       (git pre-commit)
    repo_hygiene.py --diff <ref>    check files changed      (CI gate, vs PR/push base)
    repo_hygiene.py --all           check every tracked file (full-tree audit)
    repo_hygiene.py --list          report every current violation, baseline included

Exit: 0 clean, 1 violation(s), 2 usage/config error. Known legacy violations are
grandfathered via `baseline`; any NEW violation fails. The cleanup pass shrinks
the baseline to zero.
"""
from __future__ import annotations

import argparse
import fnmatch
import subprocess
import sys
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover
    sys.stderr.write("repo_hygiene: requires Python 3.11+ (tomllib)\n")
    sys.exit(2)

# tools/governance/repo_hygiene.py -> repo root
REPO = Path(__file__).resolve().parents[2]
CONFIG = REPO / "config" / "repo-hygiene.toml"


def load_cfg() -> dict:
    if not CONFIG.is_file():
        sys.stderr.write(f"repo_hygiene: missing ruleset {CONFIG.relative_to(REPO).as_posix()}\n")
        sys.exit(2)
    with CONFIG.open("rb") as handle:
        return tomllib.load(handle)


def _git(*args: str) -> list[str]:
    out = subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, check=True)
    return [line for line in out.stdout.splitlines() if line]


def tracked_files() -> list[str]:
    return _git("ls-files")


def staged_files() -> list[str]:
    return _git("diff", "--cached", "--name-only", "--diff-filter=ACMR")


def changed_files(base: str) -> list[str]:
    # Files added/modified/renamed on HEAD since it diverged from base (PR/push diff).
    return _git("diff", "--name-only", "--diff-filter=ACMR", f"{base}...HEAD")


def _top(path: str) -> str:
    return path.split("/", 1)[0]


def _ext(name: str) -> str:
    return "." + name.rsplit(".", 1)[-1].lower() if "." in name else ""


def _doc_ok(path: str, name: str, cfg: dict) -> bool:
    if name in cfg["doc_allowed_basenames"]:
        return True
    return (_top(path) + "/") in cfg["doc_allowed_roots"]


def check(path: str, cfg: dict) -> str | None:
    """Return a one-line violation message for `path`, or None if compliant."""
    p = path.replace("\\", "/").lstrip("/")
    while p.startswith("./"):
        p = p[2:]
    name = p.rsplit("/", 1)[-1]
    ext = _ext(name)

    # Rule 7: exempt directories.
    if _top(p) in cfg["exempt_roots"]:
        return None

    # Rule 1 complement: build/ is the canonical, git-ignored artifact home, so a
    # write already targeting it is correct -- don't flag transient extensions there.
    if _top(p) == "build":
        return None

    # Rule 1/6: transient artifacts are never tracked; their only home is build/.
    if any(fnmatch.fnmatch(name, glob) for glob in cfg["transient_globs"]):
        return f"rule 1: transient artifact '{p}' must live under build/ and never be tracked"

    # Rule 3 (root): only the allowlist may sit at the repository root.
    if "/" not in p:
        if name not in cfg["root_allowlist"]:
            return f"rule 3: '{p}' must not sit at the repo root; move it under docs/, scripts/, src/, config/, or tools/"
        return None

    # Rule 2: compile/link source belongs under a source root.
    if ext in cfg["source_exts"] and (_top(p) + "/") not in cfg["source_roots"]:
        return f"rule 2: source '{p}' must live under {', '.join(cfg['source_roots'])}"

    # Rule 3: loose documentation belongs under docs/{category}/.
    if ext in cfg["doc_exts"] and not _doc_ok(p, name, cfg):
        return f"rule 3: documentation '{p}' must live under docs/{{category}}/ (or be a co-located README/NOTES/CAPSULE)"

    # Rule 5: shell scripts belong under scripts/{sub}/ or a tooling/test/ci home.
    if ext in cfg["script_exts"] and (_top(p) + "/") not in cfg["script_roots"]:
        return f"rule 5: script '{p}' must live under {', '.join(cfg['script_roots'])}"

    return None


def _normalize_arg_path(raw: str) -> str | None:
    """Repo-relative posix path, or None when `raw` lies outside the repository."""
    candidate = Path(raw)
    if not candidate.is_absolute():
        candidate = REPO / candidate
    try:
        return candidate.resolve().relative_to(REPO).as_posix()
    except (ValueError, OSError):
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Repo-hygiene structure linter (ADR-0025).")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--path", metavar="FILE", help="check a single path (Claude write hook)")
    mode.add_argument("--staged", action="store_true", help="check git-staged files (pre-commit)")
    mode.add_argument("--diff", metavar="REF", help="check files changed since REF (CI gate)")
    mode.add_argument("--all", action="store_true", help="check every tracked file (full-tree audit)")
    mode.add_argument("--list", action="store_true", help="report every current violation (baseline included)")
    args = parser.parse_args()

    cfg = load_cfg()
    baseline = set(cfg.get("baseline", []))

    if args.path is not None:
        rel = _normalize_arg_path(args.path)
        paths = [rel] if rel is not None else []
    elif args.staged:
        paths = staged_files()
    elif args.diff is not None:
        paths = changed_files(args.diff)
    else:  # --all or --list
        paths = tracked_files()

    include_baseline = args.list
    violations: list[tuple[str, str]] = []
    for path in paths:
        message = check(path, cfg)
        if message is None:
            continue
        norm = path.replace("\\", "/").lstrip("/")
        if norm in baseline and not include_baseline:
            continue
        violations.append((norm, message))

    if not violations:
        count = len(paths)
        suffix = "" if count == 1 else "s"
        print(f"repo_hygiene: clean ({count} path{suffix} checked)")
        return 0

    for _, message in sorted(violations):
        print(f"repo_hygiene: {message}")
    print(f"repo_hygiene: {len(violations)} violation(s)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
