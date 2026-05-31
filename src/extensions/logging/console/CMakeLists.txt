add_library(
    mnemos_logging_console
    console_logger_provider.cpp
)
add_library(mnemos::logging::console ALIAS mnemos_logging_console)

target_include_directories(
    mnemos_logging_console
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_logging_console PUBLIC mnemos::logging)

mnemos_apply_common_target_options(mnemos_logging_console PUBLIC)
mnemos_declare_tier(NAME mnemos_logging_console TIER 2 DEPENDS mnemos_logging)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_logging_console_test
        SOURCES tests/console_logger_test.cpp
        LIBRARIES mnemos::logging::console
    )
endif()
