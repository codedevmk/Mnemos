add_library(mnemos_common INTERFACE)
add_library(mnemos::common ALIAS mnemos_common)

target_include_directories(
    mnemos_common
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_common INTERFACE)
mnemos_declare_tier(NAME mnemos_common TIER 1)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_common_test
        SOURCES
            tests/hex_test.cpp
            tests/string_test.cpp
        LIBRARIES mnemos::common
    )
endif()
