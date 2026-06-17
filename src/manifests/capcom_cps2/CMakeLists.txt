# CPS2 (Capcom Play System 2) board data + helpers. Phase 1 delivers the CPS-2
# 68000 opcode cipher (the load-time transform that produces the decrypted
# opcode image from the encrypted program ROM + the board's 20-byte key). The
# 68000 opcode/data split, board manifest, video, and QSound land in later
# increments.
add_library(
    mnemos_manifests_capcom_cps2
    cps2_crypto.cpp
)
add_library(mnemos::manifests::capcom_cps2 ALIAS mnemos_manifests_capcom_cps2)

target_include_directories(
    mnemos_manifests_capcom_cps2
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# Phase 1 is a pure data transform: it needs only the common build options, no
# chips or bus yet.
mnemos_apply_common_target_options(mnemos_manifests_capcom_cps2 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_capcom_cps2
    TIER 4
    DEPENDS
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_capcom_cps2_crypto_test
        SOURCES tests/cps2_crypto_test.cpp
        LIBRARIES mnemos::manifests::capcom_cps2
    )
endif()
