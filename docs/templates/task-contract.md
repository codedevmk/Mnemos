# Task Contract — <short title>

L4 artifact (MNE-CTX-PLAN-001 §6.3): a capsule is what the agent knows; a
contract is what the agent must do. Free-form fields removed at will; the
`read_manifest` is what makes the context-escape metric (M7) measurable.

```yaml
contract:
  goal: >
    One or two sentences: the observable outcome, not the implementation.
  read_manifest:            # in-scope paths; reads outside this set are
    - CONSTITUTION.md       # logged and counted by M7
    - src/chips/cpu/z80/CAPSULE.md
    - src/chips/cpu/z80/
  constraints:              # ARCH/STD IDs that bind this task
    - ARCH-001
    - ARCH-004
    - STD-002
  definition_of_done:
    gates: [G1, G2, G9]     # named gates that must be green
    tests:                  # named test targets that must pass
      - z80_test
  budget_tokens: 24000      # standard context pack ceiling (D3)
```
