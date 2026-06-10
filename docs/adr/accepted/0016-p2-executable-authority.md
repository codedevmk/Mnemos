---
id: ADR-0016
title: "P2 Executable Authority — Implementation Decisions"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: 2026-06-10
---

# ADR 0016: P2 Executable Authority — Implementation Decisions

## Context

MNE-CTX-PLAN-001 P2 specifies the oracle registry, the G5 ratchet, golden
determinism (G6), invariant coverage (G4), schema compatibility (G8), and
nightly sanitizers (G12). Implementing them against the real tree — where
conformance corpora and ROMs are data-gated and never committed — forced the
following decisions.

## Decisions

1. **Ratchet granularity is suite-level, with skip-aware semantics.** The
   conformance harnesses do not yet emit per-vector JSON, so
   `tests/oracles/highwater.json` records per-suite high-water states
   (`never-run < skipped < passed`). Rules: a suite that has ever passed may
   never fail, regress to skipped (unless data-gated — corpus absence is not
   a regression), or vanish from the build. Per-vector granularity (true M9
   pass *rates*, M10 opcode coverage) is scheduled with harness JSON output.

2. **One runner, two gates.** `oracle_runner.py` serves both G5 (chip suites
   + machine parity) and G6 (machine golden suites) from the same registry;
   `golden_replay.py` as a separate tool would duplicate the CTest plumbing
   for no behavioral difference. G6's threshold remains its own: a failed
   golden suite is a determinism divergence.

3. **ORC-LEGACY-\* are the in-tree ported suites.** The plan's "311 C11
   assertions" live on as the Catch2 suites carried over per ADR-0006; the
   registry points at those CTest targets rather than re-porting anything.

4. **G6 validation is gated on data-equipped runners.** Hosted CI has no
   ROMs, so golden suites skip there by design; the ratchet records real
   passes only from runners with corpora (the owner's machine, or a future
   self-hosted runner). The P2 exit criterion "G6 = 0 on ≥ 3 golden
   scenarios" therefore needs one data-equipped `oracle_runner.py --update`
   run; four golden scenarios are registered.

5. **G8 ships with a structural parser and an explicit release procedure.**
   No `.capnp` exists yet; releasing one means snapshotting it into
   `src/debug/wire/released/`. The gate structurally diffs current vs.
   released (field removal/renumber/retype, container removal) without
   needing the capnp toolchain in CI; `capnp compile` validation layers on
   when the toolchain lands.

6. **G12 nightly = the existing ASan/UBSan preset on a cron**, in
   `.github/workflows/nightly.yml`, alongside the full oracle run whose
   high-water raises are auto-committed (the plan's zero-human ratchet).

## Fact-axis catches during P2 bring-up

Building the headless preset in a fresh environment surfaced two latent
defects, both fixed in this change set:

- `mnemos_gg_visual_corpus_test` unconditionally linked
  `mnemos::apps::player::adapters::common`, so every headless preset
  (`MNEMOS_BUILD_APPS=OFF` — the ARM64 CI jobs) failed at configure,
  violating the stated invariant that core tests do not depend on `apps/`.
  The test is now built only when apps are.
- `sprite_%02zu` into 16-byte buffers in `genesis_vdp.cpp` / `sms_vdp.cpp`
  trips `-Werror=format-truncation` on GCC 13.3 (a genuine latent
  truncation); buffers widened to cover the full `%zu` range.

## Consequences

- G4/G8 run blocking in the CI governance job; G5/G6 smoke runs on the
  `linux-gcc-release` leg; the nightly workflow carries the full ratchet
  (with auto-committed raises) and G12.
- `highwater.json` is initialized from a real local run of the full suite,
  so the ratchet floor is measured, not asserted.

## Ratification

Ratified 2026-06-10 by owner directive in the pilot session. P3 starts with
this acceptance.
