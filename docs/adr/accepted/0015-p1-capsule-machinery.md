---
id: ADR-0015
title: "P1 Capsule Machinery — Implementation Decisions"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: 2026-06-10
---

# ADR 0015: P1 Capsule Machinery — Implementation Decisions

## Context

MNE-CTX-PLAN-001 P1 specifies symbol extraction, capsule generation (G2),
doc liveness (G3), and provenance trailers (G10), with budgeted behavior but
latitude on mechanism. Building them against the real tree forced five
decisions worth recording; each is a default the weekly packet can revisit.

## Decisions

1. **Extraction engine: declaration-grammar parser first, libclang later.**
   D6 recommended libclang with the grammar parser as fallback (R1). The
   order is inverted: `extract_symbols.py` ships the restricted parser over
   clang-formatted headers because it is deterministic in every environment
   (CI, hooks, agent containers) with zero dependencies, and the extraction
   boundary is small. Promotion trigger: the first module whose public
   surface the parser visibly misparses (G2 makes that loud in the PR diff).

2. **Capsule determinism via content digests, not commit SHAs.** The plan's
   capsule front matter sketched `generated_from: <commit-sha>`; a commit SHA
   makes every commit drift every capsule. `source_digest` is the sha256 over
   the module's tracked file contents and `intent_ratified` the sha256 of the
   intent text, so regeneration is byte-identical exactly when the module is
   unchanged and `--check` is a pure byte comparison.

3. **Intent fragment = first paragraph of the module README, ≤ 150 words.**
   No new file kind, no front-matter ceremony in modules; the README lead
   already serves this role in practice (see `src/manifests/sms/README.md`).
   `gen_capsule.py` hard-fails above the word limit.

4. **G3 scope for P1: `CONSTITUTION.md`, `constitution/` modules (MNE-* plan
   documents exempt — their bodies are verbatim and name idealized/future
   layouts by design), and capsule intent fragments.** Accepted ADR bodies
   join the scope in P2 once historical-reference escapes are seeded; the
   first known catch there is ADR-0004's `i_chip` vs. the shipped `ichip`.

5. **Pilot "machine" capsule lives at `src/manifests/sms`.** The plan's
   `machines/sms` does not exist as a path; the manifests module is where a
   machine is assembled (`assemble_sms`), so it carries the machine capsule.
   Capsule test inventory associates repository tests by leaf-name match
   (`sms_*`, `*_z80_*`), a mechanical rule reported as such in the capsule.

## Consequences

- G2/G3/G10 run blocking in CI; the pre-commit fast subset covers G1/G9/G11
  plus capsule checks on touched pilot modules; a commit-msg hook enforces
  G10 locally (`tools/install-hooks.sh` installs both).
- P1 exit criteria are satisfiable: both pilot capsules are well under the
  6,000-token budget (z80 ≈ 0.5k, sms ≈ 1k).
- The G3 resolver's vocabulary lists (CMake names, lifecycle keywords,
  external API prefixes) are the tuning surface for M2 noise; changes to
  them ride normal PRs, not amendments.

## Ratification

Ratified 2026-06-10 by owner directive in the pilot session. P2 starts with
this acceptance.
