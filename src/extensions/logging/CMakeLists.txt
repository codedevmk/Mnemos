add_library(
    mnemos_logging
    ilogger.cpp
    logger_factory.cpp
)
add_library(mnemos::logging ALIAS mnemos_logging)

target_include_directories(
    mnemos_logging
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# foundation supplies the steady clock used to stamp records.
target_link_libraries(mnemos_logging PUBLIC mnemos::foundation)

mnemos_apply_common_target_options(mnemos_logging PUBLIC)
mnemos_declare_tier(NAME mnemos_logging TIER 1 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_logging_test
        SOURCES tests/logging_test.cpp
        LIBRARIES mnemos::logging
    )
endif()

add_subdirectory(console)
