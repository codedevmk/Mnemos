add_library(
    mnemos_instrumentation_api
    src/debugger.cpp
)
add_library(mnemos::instrumentation::api ALIAS mnemos_instrumentation_api)

target_include_directories(
    mnemos_instrumentation_api
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

# Tier 6: observes the runtime (tier 5) by injection; never the reverse.
target_link_libraries(mnemos_instrumentation_api PUBLIC mnemos::runtime)

mnemos_apply_common_target_options(mnemos_instrumentation_api PUBLIC)
mnemos_declare_tier(
    NAME mnemos_instrumentation_api
    TIER 6
    DEPENDS mnemos_foundation mnemos_runtime
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_instrumentation_api_test
        SOURCES tests/debugger_test.cpp
        LIBRARIES
            mnemos::instrumentation::api
            mnemos::chips::cpu::m6510
            mnemos::topology
    )
endif()
