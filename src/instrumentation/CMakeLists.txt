add_library(mnemos_instrumentation INTERFACE)
add_library(mnemos::instrumentation ALIAS mnemos_instrumentation)

target_link_libraries(
    mnemos_instrumentation
    INTERFACE
        mnemos::foundation
        mnemos::chips
        mnemos::topology
        mnemos::manifests
        mnemos::runtime
)

mnemos_apply_common_target_options(mnemos_instrumentation INTERFACE)
mnemos_declare_tier(
    NAME mnemos_instrumentation
    TIER 6
    DEPENDS mnemos_foundation mnemos_chips mnemos_topology mnemos_manifests mnemos_runtime
)

# Compiled tier-6 observability components (debugger / breakpoint engine, ...).
add_subdirectory(api)
