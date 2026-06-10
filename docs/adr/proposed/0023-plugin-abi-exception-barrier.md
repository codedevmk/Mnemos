---
id: ADR-0023
title: "APM Plugin ABI: noexcept Callback Barrier and Host-Side Validation"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: null
---

# ADR 0023: APM Plugin ABI: noexcept Callback Barrier and Host-Side Validation

## Context

The APM tracer loads the emulator as a DLL through a pure C vtable, but the
binding's callbacks were plain C++ functions: `bad_alloc` (or anything from
runtime assembly) could unwind through a C function pointer across the DLL
boundary — undefined behaviour between independently built runtimes. The
host validated only `abi_version`, so a stale plugin with null entries
crashed at first call, and `LoadLibrary`/`GetProcAddress` failures were
reported without cause. The page-guard VEH invoked user handlers while
holding a non-recursive mutex with the faulted page still protected — any
handler touching a watched page re-entered the VEH and self-deadlocked.

## Decision

Contract adopted for every present and future binding (commit b4292e0):

1. **Every function exported through the plugin vtable is `noexcept` and
   wraps throwing work in a `try/catch (...)` barrier**, reporting through
   the ABI's error returns. Leaking an exception across the ABI is a defect
   by definition, not a quality-of-implementation issue.
2. **The host validates the whole vtable** (every pointer non-null) after
   the version check, and surfaces the failing stage plus the OS error code
   via `plugin_host::error()`.
3. **The page-guard fault handler runs user callbacks only after dropping
   its lock and reopening the faulted page**; `watch()` failures roll back
   the entry instead of leaving a registered-but-unarmed watch. The
   x86-only single-step implementation is compile-gated so other Windows
   targets get an honest `supported() == false` stub.

Alternative considered: trusting bindings to be exception-free by
convention — rejected; the barrier is mechanical and the failure mode
(cross-runtime unwind) is undebuggable.

## Consequences

- A throwing binding degrades to an ABI error code instead of UB.
- Stale or partial plugins fail at load with an actionable message.
- Watch handlers may safely touch watched memory. Known remaining gaps
  (documented, not yet fixed): one instruction faulting across two watched
  pages disarms one watch; `unwatch()` racing an in-flight single-step.
  Both need a per-thread pending list and Windows-side validation.

Session: https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF
(commit b4292e0, merged via e89e460).
