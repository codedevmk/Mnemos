add_library(mnemos_frontend_sdk INTERFACE)
add_library(mnemos::frontend_sdk ALIAS mnemos_frontend_sdk)

target_link_libraries(
    mnemos_frontend_sdk
    INTERFACE
        mnemos::foundation
        mnemos::instrumentation
)

mnemos_apply_common_target_options(mnemos_frontend_sdk INTERFACE)
mnemos_declare_tier(
    NAME mnemos_frontend_sdk
    TIER 7
    DEPENDS mnemos_foundation mnemos_instrumentation
)
