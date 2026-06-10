---
id: ADR-0018
title: "Behaviour-Altering Knobs Are Chip Configuration, Not Environment Variables"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: null
---

# ADR 0018: Behaviour-Altering Knobs Are Chip Configuration, Not Environment Variables

## Context

The Genesis VDP's write-ACCEPT back-pressure model — which changes 68K stall
timing and therefore the whole-machine execution trace — was selected by the
`MNEMOS_WRITE_ACCEPT` environment variable, read once via `getenv`. Two hosts
running the same ROM and the same save state diverged if the variable
differed, and the chosen mode was deliberately not recorded in the state.
The same pattern was spreading (`MNEMOS_32X_THREAD` selects the SH-2
schedule). The framework's core thesis is deterministic, reproducible
execution.

## Decision

Options considered: (a) keep env vars and serialize them into save states;
(b) keep env vars as documented debug-only knobs; (c) require every knob
that alters the emulated execution trace to flow through the chip
`config_table` / manifest, with env vars permitted only for pure
observability (tracing, dumps).

Chosen: (c). `write_accept` is now a chip config key (default on); the env
lookup is deleted (commit 4379816). Env vars that merely *observe* (e.g.
`MNEMOS_32X_REGTRACE`, PWM dumps) remain. `MNEMOS_32X_THREAD` is tolerated
because ADR-0019's anchor unification makes both schedules emulated-state
equivalent; its remaining effect is presentation-only and is documented at
the machine header.

## Consequences

- A manifest plus a ROM fully determines the execution trace; host
  environment cannot fork it.
- Parity A/B experiments flip the same knobs through `configure()`, so the
  experiment configuration is visible in code review.
- New chips must route behaviour toggles through `config_table` from the
  start; an env-read inside a chip is now a review flag.

Session: https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF
(commit 4379816, merged via e89e460).
