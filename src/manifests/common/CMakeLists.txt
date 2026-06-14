include(FetchContent)

if(NOT TARGET tomlplusplus::tomlplusplus)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de # v3.4.0
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

add_library(
    mnemos_manifests_common
    manifest.cpp
    builder.cpp
    rom_set.cpp
    rom_set_toml.cpp
)
add_library(mnemos::manifests::common ALIAS mnemos_manifests_common)

target_include_directories(
    mnemos_manifests_common
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# Public: the builder's headers expose chips + topology types and use foundation
# (sha256). toml++ is an implementation detail of the parser (PRIVATE, ADR 0007);
# io + compression are implementation details of the ROM-set providers.
target_link_libraries(
    mnemos_manifests_common
    PUBLIC
        mnemos::foundation
        mnemos::security::cryptography
        mnemos::chips::shared
        mnemos::topology
    PRIVATE
        tomlplusplus::tomlplusplus
        mnemos::io
        mnemos::compression
)

mnemos_apply_common_target_options(mnemos_manifests_common PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_common
    TIER 4
    DEPENDS
        mnemos_foundation
        mnemos_security_cryptography
        mnemos_chips_shared
        mnemos_topology
        mnemos_io
        mnemos_compression
)

if(BUILD_TESTING)
    # The builder test creates chips by factory id; the concrete chip libraries are
    # linked so their static-init factory registrations are available, and the test
    # makes a runtime reference to each chip type so its translation unit is pulled
    # in (the registration then runs). A general "instantiate purely by id without
    # referencing the type" path for apps is handled at the runtime-CLI slice.
    mnemos_add_test(
        mnemos_manifests_common_test
        SOURCES
            tests/manifest_test.cpp
            tests/builder_test.cpp
            tests/rom_set_test.cpp
            tests/rom_set_toml_test.cpp
        LIBRARIES
            mnemos::manifests::common
            mnemos::chips::cpu::m6510
            mnemos::chips::bus_controller::cia_6526
            mnemos::io
    )
endif()
