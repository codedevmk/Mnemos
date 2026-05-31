# Debug

Cross-system debugging, built on the `mnemos::instrumentation` contract.

- `mnemos::debug::engine` (`debugger.{hpp,cpp}`) -- PC breakpoints, memory
  watchpoints, single-step / frame-step, and event subscription, driven over a
  `runtime::scheduler` with the CPU observed by an injected `cpu_probe` / bus, so
  no chip or runtime tier depends on the debugger.
- `mnemos::debug::dump` (`debug_dump.{hpp,cpp}`) -- screenshot / memory / trace
  artifact dumping over any `frontend_sdk::player_system`.

`wire/`, `scripting_lua/`, `scripting_python_ipc/` are placeholders for the
remote-debug protocol and scripting surfaces. The Commodore-64 debugger features
(disassembler, monitor, trace export, memory search/diff, call-stack) port in
here, with each CPU's disassembler living beside its chip.
