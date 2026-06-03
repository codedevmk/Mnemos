# APM tracer sidecar -- a self-contained capability, deliberately outside src/
# (the engine). It loads the emulator as a runtime plugin, injects a tagged
# memory allocator, and observes execution from the outside. See
# docs/architecture/apm-tracer-sidecar-spec.md and todo.md.
add_subdirectory(memory)
