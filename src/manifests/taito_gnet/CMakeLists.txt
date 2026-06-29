add_library(
    mnemos_manifests_taito_gnet
    taito_gnet_media.cpp
    taito_gnet_system.cpp
)
add_library(mnemos::manifests::taito_gnet ALIAS mnemos_manifests_taito_gnet)

target_include_directories(
    mnemos_manifests_taito_gnet
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_taito_gnet
    PUBLIC
        mnemos::chips::cpu::r3000a
        mnemos::disc
        mnemos::topology
    PRIVATE
        mnemos::compression
)

mnemos_apply_common_target_options(mnemos_manifests_taito_gnet PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_taito_gnet
    TIER 4
    DEPENDS
        mnemos_chips_cpu_r3000a
        mnemos_disc
        mnemos_compression
        mnemos_topology
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_taito_gnet_media_test
        SOURCES tests/taito_gnet_media_test.cpp
        LIBRARIES mnemos::manifests::taito_gnet
    )
    mnemos_add_test(
        mnemos_manifests_taito_gnet_system_test
        SOURCES tests/taito_gnet_system_test.cpp
        LIBRARIES mnemos::manifests::taito_gnet
    )
endif()
