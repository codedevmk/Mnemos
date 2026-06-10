# Constitution Migration Map

Traceability for the P0 lift (MNE-CTX-PLAN-001 §4.1): every TDS section is
mapped to its destination. Content was **lifted, not rewritten**; the original
documents under `docs/architecture/` are demoted to L3 design notes. Where a
section is not yet lifted, its disposition and lift trigger are stated — an
unlifted section remains L3 design-note authority only.

## TDS — `docs/architecture/mnemos-architecture-tds-v0.1.md`

| TDS § | Content | Disposition |
|-------|---------|-------------|
| 1 | Document control, RFC-2119 conventions | Conventions adopted repo-wide; document control superseded by the amendment process (`CONSTITUTION.md` §5) |
| 2 | Purpose, observability thesis, "what Mnemos is not" | Identity invariants lifted to `CONSTITUTION.md` §3; thesis prose remains L3 |
| 3 | Architectural principles | Layering principles → ARCH-001; determinism → ARCH-004; C++ discipline → STD-002; observability + single-API → `CONSTITUTION.md` §3 |
| 4 | Eight-tier map and notes | ARCH-001 |
| 5 | Monorepo layout | Directory tree remains L3 (descriptive); §5.1 flat-module + unique-header rules → ARCH-001 / STD-001; §5.1.1 hygiene → ARCH-001 |
| 6.1 | CMake layout, target naming | Target naming → STD-001; remainder L3, refined by ADR-0002/ADR-0009 (accepted) |
| 6.2 | Dependency enforcement | ARCH-001 (INV-ARCH-001) |
| 6.3 | Presets | L3; executable authority is `CMakePresets.json` itself |
| 6.4 | Compiler flags | STD-002; executable authority is `cmake/modules/MnemosCompilerFlags.cmake` |
| 6.5 | Dependency table | L3; executable authority is the pinned FetchContent set + `THIRD_PARTY_NOTICES.md`, gated by G7 (ARCH-005) |
| 7.1 | Language baseline, exceptions/RTTI rules | STD-002 |
| 7.2 | Development environment | L3 (tooling guidance, not contract) |
| 7.3 | Target platforms | L3; platform set has already evolved past the TDS (ARM64 CI, Android per ADR-0010 proposal) — candidate for a future ARCH module when ratified |
| 7.4 | License split | ARCH-005 (with ADR-0003) |
| 8.1–8.2, 8.4–8.6 | Chip taxonomy, ichip, ibus, introspection, registration | L3 + **ADR-0004 (accepted) is the lifted authority**; lift into a dedicated ARCH-006 chip-contract module is P1+ work, proposed via the normal pipeline |
| 8.3 | Clock contract | ARCH-004 |
| 9 | Bus and topology primitives | L3; lift pending alongside the chip-contract module |
| 10 | Manifest schema and validation rules | L3; executable authority is the manifest validator (`src/manifests/common`) |
| 11.1 | Runtime ownership | ARCH-004 |
| 11.2 | Scheduling strategy | ARCH-004 (with ADR-0005) |
| 11.3 | Determinism guarantees | ARCH-004 (INV-TIM-002) |
| 11.4–11.5 | Save state, rewind | L3 + ADR-0008 (accepted) is the lifted authority |
| 12.1 | In-process introspection API | L3; surface has evolved (see `src/instrumentation/README.md`) |
| 12.2–12.3 | Wire protocol, schema versioning | ARCH-002 |
| 13.1 | Lua embedding rules | L3; lift when scripting ships |
| 13.2 | Python out-of-process rule | ARCH-002 |
| 13.3 | Language rationale | L3 (rationale, not contract) |
| 14 | Frontend SDK contract | ARCH-003 (with a flagged candidate axis conflict re SDL3) |
| 15 | Save-state byte format | L3 + ADR-0008; executable authority is the implementation + round-trip tests |
| 16 | Determinism and replay | ARCH-004 |
| 17 | Netplay sketch | L3 (explicitly deferred design) |
| 18 | Testing strategy | Oracle/golden requirements absorbed by MNE-CTX-PLAN-001 §7 (gates G5/G6, P2); remainder L3 |
| 19 | C64 component graph | L3 (illustrative) |
| 20 | Open questions | L3 (explicit non-decisions) |

## Project plan — `docs/architecture/mnemos-project-plan-v0.1.md`

Milestone phasing, risk register, and effort sizing remain L3 planning
material. Lifted normative content: naming/governance rules (§4.4 → STD-001,
gate G1), determinism-vs-optimization ADR rule (§4.3 → ARCH-004), CI
posture (§4.1 → gate register in MNE-CTX-PLAN-001 §9).

## Todos — `docs/architecture/mnemos-todos-v0.1.md`

Historical ledger; L3 in full. The live working ledger is local-only per
`AGENTS.md`.

## AGENTS.md

Remains the agent onboarding entry point (L3 operational guidance). Its
standing rules (Eliot independence, no-GPL, scratch hygiene, ADR duty) are now
anchored in `CONSTITUTION.md` §2–§3 and ARCH-005; on any divergence the
constitution wins.
