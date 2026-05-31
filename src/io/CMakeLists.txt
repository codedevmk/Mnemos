add_library(
    mnemos_io
    file.cpp
)
add_library(mnemos::io ALIAS mnemos_io)

target_include_directories(
    mnemos_io
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_io PUBLIC)
mnemos_declare_tier(NAME mnemos_io TIER 1)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_io_test
        SOURCES tests/file_test.cpp
        LIBRARIES mnemos::io
    )
endif()
