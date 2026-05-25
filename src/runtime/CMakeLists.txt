add_library(
    mnemos_runtime
    src/scheduler.cpp
    src/input.cpp
)
add_library(mnemos::runtime ALIAS mnemos_runtime)

target_include_directories(
    mnemos_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_runtime
    PUBLIC
        mnemos::foundation
        mnemos::chips
        mnemos::topology
        mnemos::manifests
)

mnemos_apply_common_target_options(mnemos_runtime PUBLIC)
mnemos_declare_tier(
    NAME mnemos_runtime
    TIER 5
    DEPENDS mnemos_foundation mnemos_chips mnemos_topology mnemos_manifests
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_runtime_test
        SOURCES
            tests/scheduler_test.cpp
            tests/input_test.cpp
        LIBRARIES mnemos::runtime
    )
endif()
