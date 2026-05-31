# Instrumentation

The chip + runtime **introspection contract**: the observation surfaces a chip
exposes about itself -- `memory_view`, `debug_layer`, `trace_target`,
`trace_event` (declared in `chips/shared/introspection_views.hpp`, namespace
`mnemos::instrumentation`). The `mnemos::instrumentation` target bundles those
contracts with the tiers needed to consume them.

Chips *implement* these surfaces; the debugger and debugging tools (`src/debug/`)
and the player's screenshot harness *read* them. The debugger engine itself
moved to `src/debug/` -- this module is the contract it observes, not the tooling.
