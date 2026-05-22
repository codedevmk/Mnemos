add_library(mnemos_runtime INTERFACE)
add_library(mnemos::runtime ALIAS mnemos_runtime)

target_link_libraries(
    mnemos_runtime
    INTERFACE
        mnemos::foundation
        mnemos::chips
        mnemos::topology
        mnemos::manifests
)

mnemos_apply_common_target_options(mnemos_runtime INTERFACE)
mnemos_declare_tier(
    NAME mnemos_runtime
    TIER 5
    DEPENDS mnemos_foundation mnemos_chips mnemos_topology mnemos_manifests
)
