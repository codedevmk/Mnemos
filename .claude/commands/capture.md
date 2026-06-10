---
description: Session-end decision capture — draft ADR proposals for decisions actually made (MNE-CTX-PLAN-001 §5.2)
---

Review this session against the decision-capture contract in
`constitution/MNE-CTX-PLAN-001.md` §5.2 and `CONSTITUTION.md` §5:

1. Identify decisions **actually made** in this session — choices between
   real alternatives that affect architecture, contracts, dependencies,
   licensing, determinism, gates, or budgets. Routine implementation in
   line with existing authority is not a decision.

2. For each decision found, draft `docs/adr/proposed/NNNN-<slug>.md` (next
   free number — check `docs/adr/INDEX.md`) with the standard front matter
   (`status: proposed`, `proposed: <today>`, version 1.0.0) and body
   sections: Context, Decision (the options considered and why this one),
   Consequences. Reference the session and the commits involved.

3. Run `python3 tools/governance/adr_lint.py --write-index` so the index
   includes the new proposals, and re-run it in check mode to verify.

4. **Drafting nothing is a valid outcome and must be stated explicitly** —
   say "no ratifiable decisions were made this session" rather than
   manufacturing a proposal.

Never set `status: accepted` — agents propose, humans ratify (standing
rule 1).
