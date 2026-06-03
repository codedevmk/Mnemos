# APM tracer sidecar -- a self-contained capability, deliberately outside src/
# (the engine). It loads the emulator as a runtime plugin, injects a tagged
# memory allocator, and observes execution from the outside. See
# docs/architecture/apm-tracer-sidecar-spec.md and todo.md.
add_subdirectory(abi)
add_subdirectory(memory)

# The engine binding (a DLL that links the engine static libs) and the tracer host
# are Windows-focused for now: page_guard's watchpoints are Win32, and linking the
# engine's non-PIC static libs into a shared object is a Windows-only path here.
if(WIN32)
    add_subdirectory(bindings/genesis)
    add_subdirectory(host)
endif()
