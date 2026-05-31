# CLI logic as a small library so the executable and the tests share it.
add_library(mnemos_runtime_cli_core cli.cpp)
add_library(mnemos::tools::runtime_cli ALIAS mnemos_runtime_cli_core)

target_include_directories(
    mnemos_runtime_cli_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_runtime_cli_core
    PUBLIC
        mnemos::foundation
        mnemos::security::cryptography
        mnemos::manifests
        mnemos::manifests::c64
        mnemos::manifests::sms
        mnemos::runtime
)

mnemos_apply_common_target_options(mnemos_runtime_cli_core PUBLIC)
mnemos_declare_tier(
    NAME mnemos_runtime_cli_core
    TIER 8
    DEPENDS
        mnemos_foundation
        mnemos_security_cryptography
        mnemos_manifests
        mnemos_runtime
)

add_executable(mnemos_runtime_cli main.cpp)
target_link_libraries(mnemos_runtime_cli PRIVATE mnemos_runtime_cli_core)
mnemos_apply_common_target_options(mnemos_runtime_cli PRIVATE)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_runtime_cli_test
        SOURCES tests/cli_test.cpp
        LIBRARIES mnemos::tools::runtime_cli
    )
endif()
