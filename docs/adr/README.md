# Architecture Decision Records

ADRs are numbered chronologically and record decisions that affect
architecture, dependencies, licensing, determinism, build topology, or public
contracts. Numbers are never reused.

Lifecycle (`constitution/MNE-CTX-PLAN-001.md` §5, gate G9):

- `proposed/` — drafted, awaiting human ratification; expires after 14 days
  unratified.
- `accepted/` — ratified; body edits require a version bump, replacement goes
  through supersession.
- `superseded/` — replaced; front matter names the successor.

Every ADR carries YAML front matter linted by
`tools/governance/adr_lint.py`. The generated index is
[INDEX.md](INDEX.md) (`adr_lint.py --write-index`).

ADR-0006's original file was never committed; the accepted entry is a marked
reconstruction (2026-06-10).
