# Debugger engine: PC breakpoints, memory watchpoints, single-step / frame-step,
# and event subscription. Observes the runtime (tier 5) + bus (tier 3) by
# injection, never the reverse, so the chip/runtime tiers stay debugger-free.
add_library(mnemos_debug_engine debugger.cpp)
add_library(mnemos::debug::engine ALIAS mnemos_debug_engine)

target_include_directories(
    mnemos_debug_engine
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_debug_engine PUBLIC mnemos::runtime mnemos::topology)

mnemos_apply_common_target_options(mnemos_debug_engine PUBLIC)
mnemos_declare_tier(
    NAME mnemos_debug_engine
    TIER 6
    DEPENDS mnemos_foundation mnemos_topology mnemos_runtime
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_debug_engine_test
        SOURCES tests/debugger_test.cpp
        LIBRARIES
            mnemos::debug::engine
            mnemos::chips::cpu::m6510
            mnemos::topology
    )
endif()
