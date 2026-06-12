#!/usr/bin/env python3
"""Claude Code hook endpoints (MNE-CTX-PLAN-001 §5.2, §8).

Wired via .claude/settings.json:

  post-tool-use   PostToolUse hook. Appends one JSONL event per file-tool
                  call to metrics/sessions/<session_id>.jsonl — the raw
                  input for M7 (context-escape rate) and M15 (onboarding
                  reads). Logging only; never blocks a tool.
  session-end     SessionEnd/Stop hook. If the session touched authority
                  paths (constitution/, CONSTITUTION.md, wire schemas) and
                  drafted nothing under docs/adr/proposed/, records a
                  capture-omission event for the weekly packet (§5.2 — the
                  hook makes omission visible; it never auto-drafts).

Events are append-only JSONL; metrics.py aggregates them into committed
snapshots. The session logs themselves stay untracked (metrics/sessions/ is
gitignored) — raw telemetry is working data, not authority.
"""

from __future__ import annotations

import datetime as dt
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SESSIONS = REPO / "metrics" / "sessions"
AUTHORITY_PREFIXES = ("constitution/", "CONSTITUTION.md", "src/debug/wire/")
READ_TOOLS = {"Read", "Glob", "Grep", "NotebookRead"}
EDIT_TOOLS = {"Edit", "Write", "NotebookEdit"}


def log_path(session_id: str) -> Path:
    SESSIONS.mkdir(parents=True, exist_ok=True)
    safe = "".join(c for c in session_id if c.isalnum() or c in "-_") or "unknown"
    return SESSIONS / f"{safe}.jsonl"


def append(session_id: str, event: dict) -> None:
    event["ts"] = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    with log_path(session_id).open("a", encoding="utf-8") as f:
        f.write(json.dumps(event, sort_keys=True) + "\n")


def post_tool_use(payload: dict) -> None:
    tool = payload.get("tool_name", "")
    if tool in READ_TOOLS:
        kind = "read"
    elif tool in EDIT_TOOLS:
        kind = "edit"
    else:
        return
    tool_input = payload.get("tool_input") or {}
    path = tool_input.get("file_path") or tool_input.get("path") or tool_input.get("pattern")
    if not path:
        return
    append(payload.get("session_id", "unknown"),
           {"event": kind, "tool": tool, "path": str(path)})


def session_end(payload: dict) -> None:
    session_id = payload.get("session_id", "unknown")
    try:
        changed = subprocess.run(
            ["git", "-C", str(REPO), "status", "--porcelain"],
            capture_output=True, text=True, check=True,
        ).stdout
    except subprocess.CalledProcessError:
        changed = ""
    paths = [line[3:] for line in changed.splitlines() if len(line) > 3]
    touched_authority = any(p.startswith(AUTHORITY_PREFIXES) for p in paths)
    drafted = any(p.startswith("docs/adr/proposed/") for p in paths)
    append(session_id, {"event": "session_end",
                        "touched_authority": touched_authority,
                        "adr_drafted": drafted})
    if touched_authority and not drafted:
        append(session_id, {"event": "capture_omission"})


WRITE_TOOLS = {"Write", "Edit", "MultiEdit", "NotebookEdit"}


def _hygiene_message(path: str) -> str | None:
    """Return a repo-hygiene violation for `path`, or None (ADR-0025).

    Fails open: any error (missing ruleset, old Python, import failure) allows the
    write. CI is the authoritative backstop, and a broken linter must never wedge a
    session.
    """
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import repo_hygiene  # sibling module

        cfg = repo_hygiene.load_cfg()
        rel = repo_hygiene._normalize_arg_path(str(path))
        if rel is None:
            return None  # outside the repository: not this linter's jurisdiction
        if rel.lstrip("/") in set(cfg.get("baseline", [])):
            return None  # grandfathered file: editing it is fine
        return repo_hygiene.check(rel, cfg)
    except (Exception, SystemExit):
        return None


def pre_tool_use(payload: dict) -> int:
    """PreToolUse gate: block a Write/Edit aimed at a non-compliant path (ADR-0025)."""
    if payload.get("tool_name", "") not in WRITE_TOOLS:
        return 0
    tool_input = payload.get("tool_input") or {}
    path = tool_input.get("file_path") or tool_input.get("notebook_path")
    if not path:
        return 0
    message = _hygiene_message(str(path))
    if message:
        sys.stderr.write(
            "Repo-hygiene block (ADR-0025): " + message + "\n"
            "Write it to the correct location instead. Ruleset: config/repo-hygiene.toml.\n"
        )
        return 2  # exit code 2 -> Claude Code denies the tool call
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: claude_hooks.py {pre-tool-use|post-tool-use|session-end}", file=sys.stderr)
        return 2
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0  # malformed hook input must never break a session
    if sys.argv[1] == "pre-tool-use":
        return pre_tool_use(payload)
    try:
        if sys.argv[1] == "post-tool-use":
            post_tool_use(payload)
        elif sys.argv[1] == "session-end":
            session_end(payload)
    except OSError:
        pass  # logging is best-effort by design
    return 0


if __name__ == "__main__":
    sys.exit(main())
