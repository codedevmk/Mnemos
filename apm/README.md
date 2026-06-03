# APM Tracer Sidecar

A self-contained execution-tracing capability, deliberately kept **outside** the engine
(`src/`). It loads the emulator as a runtime plugin, injects a **tagged memory allocator**, and
observes execution from the **outside** — like a Dynatrace/Datadog APM agent. The engine owns no
logging, observer, or DB logic; it just runs.

Design of record: [`docs/architecture/apm-tracer-sidecar-spec.md`](../docs/architecture/apm-tracer-sidecar-spec.md).
Task list / build order: [`todo.md`](../todo.md).

## Modules

- `abi/` — the pure-C plugin + allocator contract shared by the host and the engine binding.
- `memory/` — the tagged allocator and the page-protection watchpoint engine (`page_guard`):
  VirtualProtect + a Vectored Exception Handler intercept guest-memory accesses without the
  traced program's cooperation. **(in progress — slice #1)**
- `observe/`, `collect/`, `host/`, `bindings/`, `query/` — observers/watchers, record sinks, the
  tracer host process, per-core glue, and the offline DuckDB diff tooling. *(planned)*

APM targets sit outside the engine's tier DAG (`mnemos_declare_tier`) — the tracer link-depends on
essentially nothing engine-side and loads the core as a DLL at runtime.
