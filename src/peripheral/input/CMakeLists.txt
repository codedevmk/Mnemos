add_library(
    mnemos_peripheral_input
    mk1650.cpp
    mk1653.cpp
    mk3020.cpp
)
add_library(mnemos::peripheral::input ALIAS mnemos_peripheral_input)

target_include_directories(
    mnemos_peripheral_input
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_peripheral_input
    PUBLIC
        mnemos::peripheral::common
)

mnemos_apply_common_target_options(mnemos_peripheral_input PUBLIC)
mnemos_declare_tier(
    NAME mnemos_peripheral_input
    TIER 2
    DEPENDS mnemos_peripheral_common mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_peripheral_input_mk1650_test
        SOURCES tests/mk1650_test.cpp
        LIBRARIES mnemos::peripheral::input
    )
    mnemos_add_test(
        mnemos_peripheral_input_mk1653_test
        SOURCES tests/mk1653_test.cpp
        LIBRARIES mnemos::peripheral::input
    )
    mnemos_add_test(
        mnemos_peripheral_input_mk3020_test
        SOURCES tests/mk3020_test.cpp
        LIBRARIES mnemos::peripheral::input
    )
endif()
