add_library(mnemos_peripheral_common INTERFACE)
add_library(mnemos::peripheral::common ALIAS mnemos_peripheral_common)

target_include_directories(
    mnemos_peripheral_common
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_peripheral_common INTERFACE)
mnemos_declare_tier(NAME mnemos_peripheral_common TIER 2 DEPENDS mnemos_foundation)
