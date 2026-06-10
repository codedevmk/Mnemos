---
id: CONSTITUTION
title: "Mnemos Constitution — L0 Index"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
---

# Mnemos Constitution

The thin L0 index. Every agent session loads this file unconditionally;
everything else loads on demand. Instituted by MNE-CTX-PLAN-001
(`constitution/MNE-CTX-PLAN-001.md`); adopted via ADR-0013.

## 1. Authority model — two orthogonal axes

| Axis | Order of precedence | Carrier |
|------|---------------------|---------|
| **Intent** (what the system should be) | `CONSTITUTION.md` index > `constitution/ARCH-*`, `constitution/STD-*` > `docs/adr/accepted/` > design notes | Prose + structured invariant blocks |
| **Fact** (what the system does) | Passing oracle suites & invariant tests > source > capsules > prose claims | Executable artifacts |

Precedence resolves conflicts **within** an axis only. A conflict **across**
axes (accepted doc contradicts passing test, or test contradicts accepted doc)
is never auto-resolved; it is filed as an `axis-conflict` defect and routed to
human adjudication (H3 in MNE-CTX-PLAN-001 §11). CI may attach an advisory
classification; the adjudication is human.

### Level map

| Level | Mnemos path | Mutability | Writer |
|-------|-------------|------------|--------|
| L0 | `CONSTITUTION.md` | Amendment process only (§5) | Human ratifies; agents propose |
| L1 | `constitution/ARCH-*.md`, `constitution/STD-*.md`, `docs/adr/accepted/` | Amendment / supersession | Human ratifies; agents propose |
| L2 | `src/`, `tests/`, wire schemas | Gated CI merge | Agents + human, all gates blocking |
| L3 | `*/CAPSULE.md` (generated), `docs/architecture/`, `docs/plans/`, `*/NOTES.md` | Regenerated / proposed | Generator; intent fragments human-ratified |
| L4 | Issues, task contracts | Free | Anyone |
| L5 | Chat sessions, scratch notes, other emulators | Free; **expires** | Anyone; never cited as authority |

## 2. Standing rules

1. **Agents propose; humans ratify.** No agent sets `status: accepted` on any
   L0/L1 document or merges an amendment to one without human ratification.
2. **Axis-conflict freeze.** While an `axis-conflict` defect is open, agents
   are prohibited from editing either side of the conflict.
3. **Other emulators are L5** — advisory, never an oracle. Behavioral
   authority for silicon is official documentation plus the oracle registry
   (`tests/oracles/registry.yaml`, once populated in pilot phase P2).
4. **Proposals expire.** An `adr/proposed` entry unratified after 14 days
   flips to `expired`; an unratified decision must not silently become
   load-bearing.
5. **Capsules are mechanical.** Generated capsule blocks are extraction only;
   prose synthesis beyond the human-ratified intent fragment is prohibited.
   Nothing machine-summarized enters L3 without human ratification.

## 3. Global hard invariants

These are global rather than subsystem-scoped; subsystem invariants live in
their modules.

- **No GPL/copyleft code in Apache- or MIT-licensed tiers; no third-party
  emulator source vendored.** (ARCH-005; gate G7.)
- **The runtime core is headless and deterministic**: identical manifest, ROM
  hashes, load point, and frame-tagged input produce identical output.
  (ARCH-004; golden tests, gate G6.)
- **Dependency direction is strictly downward** across the eight tiers.
  (ARCH-001; `mnemos_declare_tier`.)
- **Cycle accuracy is the default**; HLE substitutions are declared in
  manifests with rationale, never hidden.
- **Mnemos stays standalone**: no Eliot Engine runtime, UI, allocator, or
  namespace dependency without an accepted ADR introducing the boundary.
- **Observability is a product contract**: chips and runtime expose state,
  events, and timing through the instrumentation surface; frontends and
  external tools consume that surface, never runtime internals.

## 4. Module table

| ID | Summary | Version | Status |
|----|---------|---------|--------|
| [ARCH-001](constitution/ARCH-001-layering.md) | Eight-tier layering, flat modules, dependency enforcement | 1.0.0 | accepted |
| [ARCH-002](constitution/ARCH-002-wire-protocol.md) | Cap'n Proto wire contract and append-only evolution | 1.0.0 | accepted |
| [ARCH-003](constitution/ARCH-003-rendering.md) | Rendering and platform-access boundary (tier 7 only) | 1.0.0 | accepted |
| [ARCH-004](constitution/ARCH-004-timing-model.md) | Clock authority, scheduling, determinism guarantees | 1.0.0 | accepted |
| [ARCH-005](constitution/ARCH-005-licensing.md) | Apache-2.0 core / MIT chips / copyleft denylist | 1.0.0 | accepted |
| [STD-001](constitution/STD-001-naming.md) | snake_case, unique header basenames, chip IDs | 1.0.0 | accepted |
| [STD-002](constitution/STD-002-errors.md) | std::expected, no exceptions/RTTI in core, -Werror | 1.0.0 | accepted |
| [MNE-CTX-PLAN-001](constitution/MNE-CTX-PLAN-001.md) | Context-engineering pilot: gates, metrics, phases | 1.0.0 | accepted |

Traceability from the source documents: `constitution/MIGRATION.md`.

## 5. Amendment process

1. Anyone (human or agent) drafts a proposal: a new `docs/adr/proposed/` entry,
   or an edited module with `status: proposed` and a version bump.
2. Front matter and lifecycle are machine-linted by
   `tools/governance/adr_lint.py` (gate G9): valid status transitions, no
   accepted-body edits without a version bump, superseded documents name a
   successor, exactly one accepted document per ID.
3. A human ratifies (H1/H2): status flips to `accepted` with a `ratified`
   date, or to `rejected`. Unratified proposals expire after 14 days.
4. "Immutable" means **append-only with supersession**: an accepted document
   is never rewritten in place beyond version-bumped amendments; replaced
   documents move to `superseded` and name their successor.
