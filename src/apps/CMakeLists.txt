add_library(mnemos_apps INTERFACE)
add_library(mnemos::apps ALIAS mnemos_apps)

target_link_libraries(
    mnemos_apps
    INTERFACE
        mnemos::foundation
        mnemos::runtime
        mnemos::instrumentation
        mnemos::frontend_sdk
)

mnemos_apply_common_target_options(mnemos_apps INTERFACE)
mnemos_declare_tier(
    NAME mnemos_apps
    TIER 8
    DEPENDS mnemos_foundation mnemos_runtime mnemos_instrumentation mnemos_frontend_sdk
)

# The SDL3-backed windowed player. System-agnostic: it drives any
# mnemos::frontend_sdk::player_system implementation (Genesis, SMS, C64, ...).
add_subdirectory(player)
