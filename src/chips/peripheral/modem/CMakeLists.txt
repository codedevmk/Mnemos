add_library(
    mnemos_chips_peripheral_modem
    src/modem.cpp
)
add_library(mnemos::chips::peripheral::modem ALIAS mnemos_chips_peripheral_modem)

target_include_directories(
    mnemos_chips_peripheral_modem
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_peripheral_modem PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_peripheral_modem PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_peripheral_modem TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_peripheral_modem_test
        SOURCES tests/modem_test.cpp
        LIBRARIES mnemos::chips::peripheral::modem
    )
endif()
