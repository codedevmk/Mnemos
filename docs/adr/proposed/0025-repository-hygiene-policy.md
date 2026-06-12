---
id: ADR-0025
title: "Repository Hygiene and Artifact Placement Policy"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-11
ratified: null
---

# ADR 0025: Repository Hygiene and Artifact Placement Policy

## Context

The tree started clean but has repeatedly drifted: transient artifacts, loose docs, and
stray files accumulate at the root and in ad-hoc directories, and past manual cleanups
keep being undone — notably by agents (including Claude). ADR-0001 (monorepo layout) and
ADR-0009 (module placement) define where *source modules* live, but there is no enforced,
machine-checkable rule for the whole tree, so the policy survives only as prose nobody
re-reads. This ADR makes placement a gate, not a guideline.

## Decision

Seven rules govern every path in the repository.

1. **Transient artifacts live only under `build/`.** Anything build- or debug-related
   with no foundational purpose — binaries, debug symbols, asset staging, compiler/linker
   temporaries, logs, reports, test outputs, reference captures, scratch — is created
   under `build/`, organized by subfolder, and never tracked. (Supersedes the former root
   `scratch/` convention; see Taxonomy.)
2. **Compile/link source lives under `src/{name}/`** (and the established `tests/`,
   `tools/`, `apm/`, `extern/` roots). Source is never sprinkled among docs, policy, or
   artifacts.
3. **Documentation lives under `docs/{category}/`**, except `README.md` and `AGENTS.md`
   (allowed anywhere) and co-located module docs (`NOTES.md`, `CAPSULE.md`, `ROMS.md`).
4. **Stale docs are rectified or retired.** A document that no longer serves a current or
   future purpose is archived then purged; a document containing stale facts is fixed
   immediately. Repo context stays factual.
5. **Scripts live under `scripts/{sub}/`; tests under `tests/`; relocatable lint/config
   under `config/`.** Config whose tool *requires* a fixed location (`.clang-format`,
   `.clang-tidy`, `.editorconfig`) stays where the tool discovers it.
6. **`.gitignore` is kept current** so no unintended, licensed, or commercial content is
   tracked. Third-party content is admitted only as open-source used within its terms and
   recorded in `THIRD-PARTY-REFERENCES.md` / `THIRD_PARTY_NOTICES.md`.
7. **Rules 1-5 do not apply to `.claude/**` and `.github/**`.**

### Enforcement — one ruleset, three triggers

The rules are encoded once in `config/repo-hygiene.toml` and enforced by one linter,
`tools/governance/repo_hygiene.py`, behind three triggers so a violation is caught at the
earliest possible point:

- **Claude write hook** — a PreToolUse hook (`.claude/settings.json` →
  `claude_hooks.py pre-tool-use`) blocks a `Write`/`Edit`/`MultiEdit` aimed at a
  non-compliant path in real time, with the rule and the correct destination.
- **git pre-commit** — `tools/githooks/pre-commit` (installed via
  `scripts/dev/install-hooks.*`, which sets `core.hooksPath`) blocks a non-compliant
  staged file locally.
- **CI gate** — the `governance` job runs the linter over the files changed in the PR/push
  (`--diff`). This is the authoritative backstop for every change; it cannot be bypassed by
  skipping local setup. (`--all` stays available for an on-demand full-tree audit.)

A **baseline ratchet** in the ruleset grandfathers currently-known violations, so the gate
goes live immediately against *new* violations without a blocking flag day; the cleanup
pass shrinks the baseline to zero. The linter fails on any violation not in the baseline;
nothing may be added to the baseline.

### `build/` taxonomy (rule 1)

```
build/
  cmake/      out-of-source CMake trees (per preset)
  bin/        staged binaries
  scratch/    ad-hoc session debug (framebuffers, VRAM/CRAM dumps, trace CSVs)
  logs/       logs
  reports/    generated reports
  artifacts/  asset staging / reference captures
  test/       test outputs
```

All of `build/` is git-ignored; the linter enforces *staying inside* `build/`, not the
internal layout.

## Consequences

- Placement becomes mechanically enforced at three layers: agents are blocked at write
  time, developers at commit time, and the PR at CI. The repeated drift stops.
- The former root `scratch/` convention in `AGENTS.md` is replaced by `build/scratch/`.
  `OPTIMIZATIONS.md` and `todo.md` (current root violations) are baselined pending the
  cleanup pass.
- Rule 4 (stale-doc retirement) is a standing review duty, not a linter check.
- The cleanup pass (relocations + baseline drain, ADR-0025 "PR #2") is a separate,
  reviewed change so no file is moved or deleted without sign-off.
- Extends ADR-0001 and ADR-0009 (it governs the whole tree, not just source modules); on
  ratification, move this file to `docs/adr/accepted/`, set `status: accepted` + a
  `ratified` date, and regenerate `docs/adr/INDEX.md`.
