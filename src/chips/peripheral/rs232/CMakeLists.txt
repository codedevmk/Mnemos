add_library(
    mnemos_chips_peripheral_rs232
    src/rs232.cpp
)
add_library(mnemos::chips::peripheral::rs232 ALIAS mnemos_chips_peripheral_rs232)

target_include_directories(
    mnemos_chips_peripheral_rs232
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_peripheral_rs232 PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_peripheral_rs232 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_peripheral_rs232 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_peripheral_rs232_test
        SOURCES tests/rs232_test.cpp
        LIBRARIES mnemos::chips::peripheral::rs232
    )
endif()
