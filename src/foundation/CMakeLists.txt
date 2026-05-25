add_library(mnemos_foundation INTERFACE)
add_library(mnemos::foundation ALIAS mnemos_foundation)

# version.hpp is generated from the single CMake project version to avoid drift.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/mnemos/foundation/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/mnemos/foundation/version.hpp"
    @ONLY
)

target_include_directories(
    mnemos_foundation
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_foundation INTERFACE)
mnemos_declare_tier(NAME mnemos_foundation TIER 1)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_foundation_smoke_test
        SOURCES
            tests/foundation_allocator_test.cpp
            tests/foundation_bits_test.cpp
            tests/foundation_expected_ext_test.cpp
            tests/foundation_fs_test.cpp
            tests/foundation_id_test.cpp
            tests/foundation_sha256_test.cpp
            tests/foundation_smoke_test.cpp
            tests/foundation_log_test.cpp
            tests/foundation_span_ext_test.cpp
            tests/foundation_thread_test.cpp
            tests/foundation_time_test.cpp
        LIBRARIES mnemos::foundation
    )
endif()
