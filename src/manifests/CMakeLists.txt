add_library(mnemos_manifests INTERFACE)
add_library(mnemos::manifests ALIAS mnemos_manifests)

target_link_libraries(
    mnemos_manifests
    INTERFACE
        mnemos::foundation
        mnemos::chips
        mnemos::topology
)

mnemos_apply_common_target_options(mnemos_manifests INTERFACE)
mnemos_declare_tier(
    NAME mnemos_manifests
    TIER 4
    DEPENDS mnemos_foundation mnemos_chips mnemos_topology
)
