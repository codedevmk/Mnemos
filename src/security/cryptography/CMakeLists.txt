add_library(mnemos_security_cryptography INTERFACE)
add_library(mnemos::security::cryptography ALIAS mnemos_security_cryptography)

target_include_directories(
    mnemos_security_cryptography
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_security_cryptography INTERFACE)
mnemos_declare_tier(NAME mnemos_security_cryptography TIER 1)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_security_cryptography_test
        SOURCES
            tests/crc32_test.cpp
            tests/sha256_test.cpp
        LIBRARIES mnemos::security::cryptography
    )
endif()
