---
id: MNE-CTX-PLAN-001
title: "Mnemos Context-Engineering Pilot — Implementation Plan"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
---

# Mnemos Context-Engineering Pilot — Implementation Plan

> Body reproduced verbatim as proposed. Repository-reality adaptations
> (`src/` for `source/`, `docs/adr/` for `adr/`, adoption ADR numbered 0013)
> are recorded as amendments in ADR-0013, not edited into this text.

|Field      |Value                                                                                |
|-----------|-------------------------------------------------------------------------------------|
|Document ID|MNE-CTX-PLAN-001                                                                     |
|Version    |0.1                                                                                  |
|Status     |proposed (awaiting ratification — see §16)                                           |
|Date       |2026-06-10                                                                           |
|Scope      |Mnemos repository only; Eliot Engine adoption is out of scope until pilot exit review|
|Supersedes |—                                                                                    |

-----

## 1. Purpose and Success Definition

Pilot the ratified context-engineering framework (authority hierarchy, executable truth, deterministic derived views, entropy pump) on Mnemos, with automation as the default and human intervention restricted to three enumerated ratification points (§11).

**Epistemic rule for this document.** Every number herein belongs to exactly one class, and is labeled as such:

- **[GATE]** — hard machine-enforced threshold. Absolute by construction (zero violations, 100% coverage). No baseline needed.
- **[BUDGET]** — proposed operating ceiling (token limits, CI runtimes, human-action counts). Confirmed or adjusted in §16.
- **[HYP]** — improvement hypothesis. Validated or falsified against week-0 instrumented baselines (§13). No synthetic baselines appear in this document; all baselines are measured before targets are evaluated.

**Pilot success** = all [GATE]s green for 2 consecutive weeks at P3, human ratification actions within [BUDGET], and each [HYP] either validated or falsified with a ratified tuning ADR in response. Falsification with a corrective ADR counts as success — the failure mode is *unmeasured* operation, not a missed target.

-----

## 2. Authority Model — Mnemos Instantiation

### 2.1 Two orthogonal axes

|Axis                                  |Order of precedence                                                                                   |Carrier                            |
|--------------------------------------|------------------------------------------------------------------------------------------------------|-----------------------------------|
|**Intent** (what the system should be)|`CONSTITUTION.md` index > `constitution/ARCH-*`, `constitution/STD-*` > `adr/accepted/` > design notes|Prose + structured invariant blocks|
|**Fact** (what the system does)       |Passing oracle suites & invariant tests > source > capsules > prose claims                            |Executable artifacts               |

Precedence resolves conflicts **within** an axis only. A conflict **across** axes (accepted doc contradicts passing test, or test contradicts accepted doc) is never auto-resolved; it is filed as an `axis-conflict` defect and routed to H3 (§11). CI attaches an advisory classification — e.g., *doc untouched 90+ days, code changed this week → suspect stale doc* — but the adjudication is human. Agents are prohibited (via `CONSTITUTION.md` standing rule) from editing either side of an open axis-conflict.

### 2.2 Level map

|Level|Mnemos path                                                       |Mutability                   |Writer                                    |
|-----|------------------------------------------------------------------|-----------------------------|------------------------------------------|
|L0   |`CONSTITUTION.md`                                                 |Amendment process only (§4.3)|Human ratifies; agents propose            |
|L1   |`constitution/ARCH-*.md`, `constitution/STD-*.md`, `adr/accepted/`|Amendment / supersession     |Human ratifies; agents propose            |
|L2   |`source/`, `tests/`, `schemas/*.capnp`                            |Gated CI merge               |Agents + human, all gates blocking        |
|L3   |`*/CAPSULE.md` (generated), `docs/design/`                        |Regenerated / proposed       |Generator; intent fragments human-ratified|
|L4   |Issues, task contracts                                            |Free                         |Anyone                                    |
|L5   |Chat sessions, scratch notes                                      |Free; **expires**            |Anyone; never cited as authority          |

Standing rule (lifted into `CONSTITUTION.md`): *other emulators are L5 — advisory, never an oracle.* Behavioral authority for silicon is official documentation plus the oracle registry (§7.2). This closes the Yabause-as-oracle question permanently.

-----

## 3. Repository Layout

```
mnemos/
  CONSTITUTION.md                  # L0 thin index — §4
  constitution/
    ARCH-001-layering.md           # lifted from existing TDS tier definitions
    ARCH-002-wire-protocol.md      # Cap'n Proto contract + evolution rules
    ARCH-003-rendering.md          # Vulkan-first
    ARCH-004-timing-model.md       # clock authority, scheduling, determinism
    ARCH-005-licensing.md          # Apache-2.0 core / MIT chips / denylist
    STD-001-naming.md              # snake_case etc.; backed by validator
    STD-002-errors.md
  adr/
    proposed/                      # agent-drafted, expiring
    accepted/
    superseded/
    INDEX.md                       # generated
  schemas/                         # .capnp — executable wire authority
  source/                          # eight tiers per ARCH-001; each subsystem:
    <tier>/<subsystem>/
      public/                      # extraction boundary (existing convention)
      private/
      README.md                    # human intent fragment (input to capsule)
      CAPSULE.md                   # generated — never hand-edited
  tests/
    oracles/
      registry.yaml                # §7.2
      vectors/                     # JSON vectors, test ROM manifests
    golden/                        # determinism hashes — §7.3
    unit/
  tools/                           # §8 — all gate and generator scripts
  metrics/
    snapshots/YYYY-MM-DD.json      # nightly
    DASHBOARD.md                   # generated, checked in
    packets/YYYY-WW.md             # weekly review packet
```

Capsules are co-located with their subsystem (decision D2), not centralized: agents discover them by proximity, and `git log` on a subsystem shows code and capsule churn together.

-----

## 4. Constitution Bootstrap

### 4.1 Method: extraction, not authorship

The existing Mnemos TDS, project plan, and todos are the source material. P0 work is **lifting** their normative content into `constitution/` modules with stable IDs and a traceability map (`constitution/MIGRATION.md`: TDS section → ARCH/STD ID), then demoting the originals to L3 design notes. No content is rewritten in P0 — rewriting during migration is how silent semantic drift enters the authority layer.

### 4.2 Index specification

`CONSTITUTION.md` contains, in order: (1) the two-axis precedence rules of §2 verbatim, (2) the standing rules (single-writer, axis-conflict prohibition, L5-emulator rule, expiry rule), (3) hard invariants that are global rather than subsystem-scoped, (4) the module table — ID, one-line summary, version, status — pointing into `constitution/`, (5) the amendment process. Nothing else.

- **[BUDGET]** Index ≤ 8,000 tokens, measured by `tools/token_budget.py` (tokenizer-counted, enforced as advisory gate G11). The index is what every agent session loads unconditionally; its size is the per-session fixed cost.
- **[BUDGET]** Each `ARCH-*`/`STD-*` module ≤ 12,000 tokens; modules load on demand per task contract.

### 4.3 Amendment lifecycle (machine-lintable)

Every L0/L1 document carries YAML front matter:

```yaml
id: ARCH-004
title: Timing model
status: accepted        # proposed | accepted | superseded | expired
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-14
invariants: [...]        # §7.1
```

`tools/adr_lint.py` (gate G9) enforces: valid status transitions, no edits to `accepted` bodies without a version bump, `superseded` documents must name a successor, and exactly one `accepted` document per ID. "Immutable" is thereby replaced with **append-only with supersession** — enforced by tooling, not discipline.

-----

## 5. ADR Lifecycle and Decision Capture (the entropy pump)

### 5.1 States and expiry

`proposed → accepted | rejected | expired`. **[BUDGET]** Proposals expire automatically after 14 days without ratification (`adr_lint` flips status, weekly packet lists expiries). Expiry is deliberate: an unratified decision must not silently become load-bearing.

### 5.2 Session-end capture

Two mechanisms, redundant by design:

1. **`/capture` slash command** (fits the existing slash-command tooling pattern): the agent reviews the session, drafts zero or more `adr/proposed/ADR-XXXX-*.md` files containing only decisions actually made — problem, options, decision, consequences, session reference. Drafting nothing is a valid outcome and must be stated explicitly.
1. **Claude Code `SessionEnd`/`Stop` hook**: if the session touched `constitution/`, `schemas/`, or any `public/` path and produced no `adr/proposed/` file, the hook emits a reminder record into the weekly packet. The hook never auto-drafts — capture quality requires the in-context agent; the hook only makes *omission* visible.
1. **Backstop in CI** (gate G10): commits touching `constitution/` or `schemas/` must carry a `Refs: ADR-XXXX|ARCH-XXX` trailer. Scope is deliberately narrow (decision D4) — trailers on every commit would be ceremony, not signal.

ADR-0001, by construction, is "Adopt MNE-CTX-PLAN-001": ratifying this plan exercises the pipeline it specifies.

-----

## 6. Capsules

### 6.1 Composition

`CAPSULE.md` = deterministic assembly of five blocks. One is human; four are extracted.

|Block               |Source                                                     |Method                             |
|--------------------|-----------------------------------------------------------|-----------------------------------|
|Intent              |subsystem `README.md` (≤150 words, ratified)               |verbatim include + ratification SHA|
|Public API          |`public/` headers                                          |symbol extraction (D6)             |
|Dependencies        |build graph (CMake targets) + include edges                |deterministic walk                 |
|Test & oracle status|`tests/` inventory + `registry.yaml` + last nightly results|generated table                    |
|Freshness           |git                                                        |front matter below                 |

```yaml
capsule: source/tier_chips/z80
generated_from: <commit-sha>
generator: gen_capsule/0.x
intent_ratified: <sha>
token_estimate: 4870
```

- **[BUDGET]** Capsule ≤ 6,000 tokens. Exceeding the budget is a design smell (subsystem too large or API too wide), surfaced in the weekly packet rather than silently truncated.
- **Anti-hallucination rule (standing, L0):** generated blocks are mechanical extraction only. Any prose synthesis beyond the ratified intent fragment is prohibited in capsules. Nothing machine-summarized enters L3 without H2 ratification.

### 6.2 Drift detection

`gen_capsule.py --check` regenerates in a temp dir and diffs against the checked-in capsule; nonzero diff on a subsystem touched by the change fails gate G2. Drift is therefore *visible in the PR diff* — the reviewer (human or agent) sees exactly which extracted fact changed, for free.

### 6.3 Task contracts

A capsule is what the agent knows; a contract is what the agent must do. `task contracts` (L4) are small YAML/markdown blocks: goal, in-scope paths (the **read manifest**), constraints (ARCH/STD IDs that bind), definition of done (named gates + tests). The read manifest is what makes metric M7 (context-escape rate) measurable.

- **[BUDGET]** Standard task context pack — `CONSTITUTION.md` + 1–2 capsules + contract + working diff — ≤ 24,000 tokens.

-----

## 7. Executable Authority

### 7.1 Invariant blocks

Normative claims in L1 documents migrate, wherever expressible, into structured blocks:

```yaml
invariants:
  - id: INV-TIM-003
    statement: "The master clock is the sole time authority; no chip self-advances."
    verified_by: [tests/unit/scheduler/clock_authority_test]
  - id: INV-WIRE-001
    statement: "Released Cap'n Proto schemas evolve append-only."
    verified_by: [tools/schema_compat.py]
```

Gate G4: every invariant on an `accepted` document maps to ≥1 existing, passing verifier. **[GATE]** 100% on accepted documents. Claims that cannot yet be made executable remain prose and are counted by M4 — a measured debt, not a hidden one.

### 7.2 Oracle registry

`tests/oracles/registry.yaml`: per chip, the authoritative suites with provenance and pass criteria. Population is P2 work; high-confidence anchors are listed now, gaps are registered as risks rather than papered over:

|Chip                            |Anchor oracles (provenance required per entry)                       |Status           |
|--------------------------------|---------------------------------------------------------------------|-----------------|
|Z80                             |ZEXDOC/ZEXALL; SingleStepTests JSON per-opcode vectors               |known-available  |
|MOS 6510                        |Wolfgang Lorenz suite; full undocumented-opcode coverage carried over|known-available  |
|MC68000                         |SingleStepTests vectors; published timing tables                     |known-available  |
|Genesis VDP                     |VDP FIFO timing test ROM; datasheet timing tables                    |known-available  |
|SH-2 (SH7604)                   |SH7604 hardware manual–derived micro-vectors (to be authored)        |**gap — risk R3**|
|SMS VDP, SN76489, YM2612, others|survey + datasheet-derived vectors during P2                         |survey           |

The 311 passing assertions from the prior C11 implementation are ported verbatim as `ORC-LEGACY-*` — an immediate, factual regression floor.

**Ratchet mechanic (gate G5):** per-chip pass rate is stored as a high-water mark in `tests/oracles/highwater.json`; CI fails any change that lowers it and auto-commits raises. Pass rates therefore only move up, with zero human involvement.

### 7.3 Determinism harness

For each pilot machine: scripted input + ROM → xxhash of canonical machine-state snapshot every N frames, committed under `tests/golden/`. Gate G6: **[GATE]** zero divergences on replay. Divergence output is bisectable by frame index — the harness localizes regressions as a side effect. Cross-platform (x64/ARM) replay is deferred to post-pilot.

### 7.4 Wire-protocol gate

`tools/schema_compat.py`: `capnp compile` all schemas, then structural diff against the last released schema set — field renumbering, type changes, or removals on released schemas fail gate G8. The Cap'n Proto contract is thereby executable authority, not prose.

### 7.5 License gate

Gate G7: header check (Apache-2.0 under core paths, MIT under the chip library path per ARCH-005) plus dependency license scan with a copyleft denylist. **[GATE]** zero violations.

-----

## 8. Automation Inventory

All tools live in `tools/`, are deterministic (same inputs → byte-identical outputs), exit 0/1, and emit machine-readable JSON findings alongside human-readable text.

|Tool                        |Trigger                   |Runtime [BUDGET]            |On failure                                |
|----------------------------|--------------------------|----------------------------|------------------------------------------|
|`token_budget.py`           |pre-commit, CI            |< 5 s                       |advisory (G11)                            |
|`adr_lint.py`               |pre-commit, CI            |< 5 s                       |block (G9)                                |
|`extract_symbols.py`        |CI, nightly               |< 60 s                      |block (feeds G3)                          |
|`doc_liveness.py`           |CI                        |< 30 s                      |block (G3)                                |
|`gen_capsule.py` / `--check`|CI on touched subsystems  |< 30 s                      |block (G2)                                |
|`schema_compat.py`          |CI when `schemas/` touched|< 10 s                      |block (G8)                                |
|`license_audit.py`          |CI                        |< 30 s                      |block (G7)                                |
|`oracle_runner.py`          |CI smoke / nightly full   |smoke < 3 min; full < 60 min|block (G5)                                |
|`golden_replay.py`          |CI smoke / nightly full   |smoke < 2 min               |block (G6)                                |
|`metrics.py`                |nightly                   |< 5 min                     |advisory; writes snapshot + `DASHBOARD.md`|
|`weekly_packet.py`          |weekly (CI cron)          |< 5 min                     |advisory; writes `metrics/packets/`       |

Hooks: pre-commit (fast subset: G1 lint/format, G9, G11, capsule check on touched paths, **[BUDGET]** ≤ 10 s total); commit-msg (G10 trailer on scoped paths); Claude Code `PostToolUse` (logs file reads vs. the task-contract read manifest → M7) and `SessionEnd` (§5.2). CI PR suite **[BUDGET]** ≤ 5 minutes wall; nightly ≤ 60 minutes. Gate latency itself is a tracked metric (M14) — a gate suite that grows slow becomes friction, and friction is how gates get bypassed.

`doc_liveness.py` definition: every backtick-quoted identifier in L0/L1 documents and capsule intent fragments must exist in the extracted symbol inventory (with an explicit `\nolint` escape for deliberately historical references). **[GATE]** zero dead references.

-----

## 9. Gate Register

|ID |Check                                                    |Threshold                              |Blocking                |Phase live|
|---|---------------------------------------------------------|---------------------------------------|------------------------|----------|
|G1 |Build: `-Werror`, clang-format, naming validator (ported)|0 findings **[GATE]**                  |yes                     |P0        |
|G2 |Capsule drift on touched subsystems                      |0 diff **[GATE]**                      |yes                     |P1        |
|G3 |Dead references in L0/L1/intent                          |0 **[GATE]**                           |yes                     |P1        |
|G4 |Invariant coverage on `accepted` docs                    |100% **[GATE]**                        |yes                     |P2        |
|G5 |Oracle ratchet (per-chip pass rate vs. high-water mark)  |non-decreasing **[GATE]**              |yes                     |P2        |
|G6 |Golden-hash determinism replay                           |0 divergence **[GATE]**                |yes                     |P2        |
|G7 |License headers + dependency audit                       |0 violations **[GATE]**                |yes                     |P0        |
|G8 |Cap'n Proto schema compatibility                         |0 breaks on released schemas **[GATE]**|yes                     |P2        |
|G9 |ADR/doc front-matter lint, status transitions            |0 findings **[GATE]**                  |yes                     |P0        |
|G10|Provenance trailer on `constitution/`, `schemas/` commits|present **[GATE]**                     |yes                     |P1        |
|G11|Token budgets (index, modules, capsules)                 |within [BUDGET]                        |advisory → weekly packet|P1        |
|G12|Sanitizer suite (ASan/UBSan) on unit + smoke oracle runs |0 findings **[GATE]**                  |yes (nightly)           |P2        |

Advisory gates never block merges; they feed the weekly packet. Promotion of an advisory gate to blocking is itself an ADR.

-----

## 10. Metric Register

All metrics are computed by `metrics.py` from repository state, CI logs, and hook logs — no manual entry anywhere in the pipeline.

|ID |Metric                    |Formula                                                              |Source                       |Cadence|Class            |
|---|--------------------------|---------------------------------------------------------------------|-----------------------------|-------|-----------------|
|M1 |Capsule drift count       |subsystems where `--check` ≠ 0                                       |CI                           |per-PR |feeds G2         |
|M2 |Dead references           |liveness findings                                                    |CI                           |per-PR |feeds G3         |
|M3 |Invariant coverage        |linked-passing / declared, on `accepted` docs                        |CI                           |nightly|feeds G4         |
|M4 |Prose-only claim ratio    |RFC-2119 keyword lines lacking `verified_by` / total, in L1          |`doc_liveness.py`            |weekly |trend (debt)     |
|M5 |Authority churn           |L0/L1 lines changed / week                                           |git                          |weekly |trend (stability)|
|M6 |Tokens per accepted change|Σ session tokens / merged changes                                    |Claude Code telemetry (D8)   |weekly |[HYP] H1         |
|M7 |Context-escape rate       |reads outside task read-manifest / total reads                       |PostToolUse log              |weekly |[HYP] H2         |
|M8 |First-pass acceptance     |changes with no fixup/revert within 48 h / total                     |git                          |weekly |[HYP] H3         |
|M9 |Oracle pass rate          |per chip, passed / total vectors                                     |nightly                      |nightly|ratchet G5       |
|M10|Opcode vector coverage    |opcodes with ≥1 vector / documented opcode space (incl. undocumented)|registry                     |weekly |trend            |
|M11|Determinism divergences   |count                                                                |nightly                      |nightly|feeds G6         |
|M12|Decision latency          |median days `proposed → accepted`                                    |adr front matter             |weekly |trend            |
|M13|Proposal expiry rate      |expired / proposed                                                   |adr front matter             |weekly |trend            |
|M14|Gate latency              |PR suite wall-clock p50/p95                                          |CI                           |weekly |vs. [BUDGET]     |
|M15|Onboarding reads          |files read before first edit, per session                            |PostToolUse log              |weekly |[HYP] H4         |
|M16|Human ratification actions|count of H1–H3 actions / week                                        |adr front matter + packet log|weekly |vs. [BUDGET]     |

-----

## 11. Human Intervention Model

The complete set of human touchpoints. Anything not listed here is automated; any new recurring human step requires an ADR.

|ID|Action                                      |Trigger                                     |Cadence            |
|--|--------------------------------------------|--------------------------------------------|-------------------|
|H1|Ratify / reject `adr/proposed/` entries     |weekly packet                               |batched, weekly    |
|H2|Ratify intent fragments and L0/L1 amendments|PR touching those paths                     |as proposed        |
|H3|Adjudicate `axis-conflict` defects          |CI-filed defect with advisory classification|rare, on occurrence|

- **[BUDGET]** ≤ 6 ratification actions per week steady-state (M16), serviced from a single weekly packet read.
- **Weekly packet** (`weekly_packet.py`, generated, zero human assembly): metric deltas vs. prior week, pending and expiring proposals, advisory-gate findings, capsule budget breaches, and — the tuning loop — the single largest metric regression paired with an auto-drafted corrective proposal in `adr/proposed/`. Operational fine-tuning therefore flows through the same ratification pipe as everything else: the system proposes its own adjustments; the human's role is ratify/reject.

-----

## 12. Phased Execution

|Phase                                   |Content                                                                                                                                                                                                                                                     |Entry                    |Exit criteria (all measurable)                                                                                                             |
|----------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------|-------------------------------------------------------------------------------------------------------------------------------------------|
|**P0 — Bootstrap** (~3 working days)    |Repo scaffold (§3); lift TDS → `constitution/` with `MIGRATION.md`; `CONSTITUTION.md` index; ADR scaffold + ADR-0001; gates G1, G7, G9 wired; **week-0 baseline capture**: one instrumented agent session per pilot subsystem recording M6/M7/M15 raw inputs|Ratification of this plan|G1/G7/G9 green; index ≤ 8k tokens; `MIGRATION.md` covers 100% of TDS normative sections; baselines stored in `metrics/snapshots/week0.json`|
|**P1 — Capsule machinery** (~2 weeks)   |`extract_symbols`, `gen_capsule`, `doc_liveness`, trailer check; pilot capsules: `chips/z80`, `machines/sms` (D1); task-contract template; hooks live                                                                                                       |P0 exit                  |G2/G3/G10 blocking on pilot paths; both capsules ≤ 6k tokens; PR suite ≤ 5 min (M14)                                                       |
|**P2 — Executable authority** (~2 weeks)|Oracle registry populated for pilot chips; `ORC-LEGACY-*` ported; ratchet + high-water file; invariant blocks in accepted ARCH docs; golden-hash harness for SMS machine; schema-compat gate; sanitizers in nightly                                         |P1 exit                  |G4 = 100% on accepted docs; G5 ratchet live with M9 ≥ legacy parity; G6 = 0 on ≥3 golden scenarios; G8/G12 live                            |
|**P3 — Operations** (steady state)      |`/capture` + SessionEnd hook; telemetry ingestion (D8); `metrics.py`, `DASHBOARD.md`, weekly packet; tuning loop active                                                                                                                                     |P2 exit                  |2 consecutive packets generated with zero manual assembly; M16 ≤ 6/week; all [HYP] evaluations scheduled                                   |

Pilot subsystem rationale (D1): **Z80** is the maximally shared chip (SMS, Genesis sound, CPS1 sound) with the richest oracle anchors — it exercises every chip-level gate at minimum surface. **SMS** is the smallest complete machine (Z80 + VDP + SN76489) — it exercises the integration-level gates (G6 golden replay) without Saturn-scale scope. Together they cover both verification axes; nothing about the tooling is specific to them, so rollout to remaining tiers is mechanical.

-----

## 13. Improvement Hypotheses

Evaluated at P3 + 4 weeks against week-0 baselines captured in P0. Each hypothesis names its falsification action — falsification triggers a corrective proposal, not a shrug.

|ID|Metric                     |Target         |If falsified                                                                            |
|--|---------------------------|---------------|----------------------------------------------------------------------------------------|
|H1|M6 tokens / accepted change|−40% vs. week-0|Inspect top-5 token sessions; auto-draft ADR adjusting capsule scope or index content   |
|H2|M7 context-escape rate     |< 15%          |Identify most-escaped paths; auto-draft ADR widening capsules or splitting subsystems   |
|H3|M8 first-pass acceptance   |≥ 80%          |Correlate failures with gate IDs; tighten contracts or add missing executable invariants|
|H4|M15 onboarding reads       |−50% vs. week-0|Index content audit — what agents still hunt for belongs in L0 or capsules              |

-----

## 14. Deferred — with Quantified Promotion Triggers

|Item                              |Status            |Promotion trigger                                                                             |
|----------------------------------|------------------|----------------------------------------------------------------------------------------------|
|Retrieval index (vector/graph/RAG)|not built         |M7 > 20% for 4 consecutive weeks **while** capsule coverage ≥ 80% of subsystems               |
|Architect-agent judgment pass     |not built         |≥ 3 ratified `axis-conflict` ADRs describing a drift class no deterministic linter can express|
|Multi-agent supervisor hierarchy  |out of pilot scope|post-pilot review only                                                                        |
|Cross-platform determinism replay |not built         |second target platform enters CI                                                              |

Nothing on this list is built speculatively; each has a measured condition under which building it stops being speculation.

-----

## 15. Risks

|ID|Risk                                                                     |Mitigation                                                                                                                                                                                                                            |
|--|-------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|R1|C++23 symbol extraction fragility (modules, deduced types) under libclang|Extraction boundary is `public/` headers only (existing convention keeps the surface conservative); fallback per D6 is a declaration-grammar parser over that restricted subset; extractor failures are loud (G3 blocks), never silent|
|R2|RFC-2119 heuristic noise in M4                                           |M4 is trend-only, never gating; per-line `\nolint` escape; tune keyword list via weekly packet                                                                                                                                        |
|R3|SH-2 oracle thinness                                                     |Authoring SH7604 manual-derived micro-vectors is scheduled P2+ work with its own coverage metric (M10 per chip); other emulators remain L5 advisory; SH-2 chip ratification blocked on oracle floor, not on schedule pressure         |
|R4|Goodhart on M6 (gaming token counts degrades quality)                    |M6 is never read alone — packet renders M6 and M8 as a pair; M6 improvement with M8 regression flags as a finding                                                                                                                     |
|R5|Gate latency creep → gate bypassing                                      |M14 tracked weekly against [BUDGET]; breach auto-drafts a corrective proposal (split smoke/full, cache extraction)                                                                                                                    |
|R6|Capture ritual decay (sessions end without `/capture`)                   |SessionEnd hook makes omission visible in the packet (§5.2); omission count is a packet line item                                                                                                                                     |

-----

## 16. Decision Register — Ratification Required

Recommendations marked ▸. Ratifying D1–D8 (with any amendments) constitutes acceptance of this plan as ADR-0001 and starts P0.

|ID|Decision                |Options                                                          |▸ Recommendation                               |
|--|------------------------|-----------------------------------------------------------------|-----------------------------------------------|
|D1|Pilot subsystems        |any chip + machine pair                                          |▸ `chips/z80` + `machines/sms` (§12 rationale) |
|D2|Capsule placement       |co-located `CAPSULE.md` vs. central `/capsules` tree             |▸ co-located (§3)                              |
|D3|Token budgets           |confirm 8k / 12k / 6k / 24k                                      |▸ as proposed; revisit at P3+4wk with M6 data  |
|D4|Provenance trailer scope|all commits vs. `constitution/` + `schemas/` only                |▸ narrow scope (§5.2)                          |
|D5|Proposal expiry window  |7 / 14 / 30 days                                                 |▸ 14 days                                      |
|D6|Symbol extraction       |libclang vs. restricted declaration-grammar parser over `public/`|▸ libclang first; grammar parser as R1 fallback|
|D7|CI provider             |GitHub Actions assumed (public OSS repo)                         |▸ confirm                                      |
|D8|Token telemetry source  |Claude Code OpenTelemetry export vs. session-log parsing         |▸ OTel export; log parsing as fallback         |

-----

*MNE-CTX-PLAN-001 v0.1 — proposed. On ratification: status → accepted, this file moves under `constitution/`, and ADR-0001 records the adoption.*
