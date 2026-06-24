add_library(
    mnemos_chips_peripheral_rp5c01
    rp5c01.cpp
)
add_library(mnemos::chips::peripheral::rp5c01 ALIAS mnemos_chips_peripheral_rp5c01)

target_include_directories(
    mnemos_chips_peripheral_rp5c01
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_peripheral_rp5c01 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_peripheral_rp5c01 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_peripheral_rp5c01 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_peripheral_rp5c01_test
        SOURCES tests/rp5c01_test.cpp
        LIBRARIES mnemos::chips::peripheral::rp5c01
    )
endif()
