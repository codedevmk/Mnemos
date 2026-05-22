add_library(mnemos_topology INTERFACE)
add_library(mnemos::topology ALIAS mnemos_topology)

target_link_libraries(mnemos_topology INTERFACE mnemos::foundation mnemos::chips)

mnemos_apply_common_target_options(mnemos_topology INTERFACE)
mnemos_declare_tier(NAME mnemos_topology TIER 3 DEPENDS mnemos_foundation mnemos_chips)
