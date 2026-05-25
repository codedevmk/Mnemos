add_library(
    mnemos_chips_peripheral_reu
    reu.cpp
)
add_library(mnemos::chips::peripheral::reu ALIAS mnemos_chips_peripheral_reu)

target_include_directories(
    mnemos_chips_peripheral_reu
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_peripheral_reu PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_peripheral_reu PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_peripheral_reu TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_peripheral_reu_test
        SOURCES tests/reu_test.cpp
        LIBRARIES mnemos::chips::peripheral::reu
    )
endif()
