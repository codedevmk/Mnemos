add_subdirectory(common)
add_subdirectory(c64)
add_subdirectory(genesis)
add_subdirectory(sms)
add_subdirectory(segacd)
add_subdirectory(sega32x)
add_subdirectory(irem_m72)
add_subdirectory(taito_f2)
add_subdirectory(taito_gnet)
add_subdirectory(capcom_cps1)
add_subdirectory(capcom_cps2)
add_subdirectory(spectrum)
add_subdirectory(nes)
add_subdirectory(msx)
add_subdirectory(amiga500)
add_subdirectory(msx2)

add_library(mnemos_manifests INTERFACE)
add_library(mnemos::manifests ALIAS mnemos_manifests)

target_link_libraries(mnemos_manifests INTERFACE mnemos::manifests::common)

mnemos_apply_common_target_options(mnemos_manifests INTERFACE)
mnemos_declare_tier(
    NAME mnemos_manifests
    TIER 4
    DEPENDS mnemos_foundation mnemos_chips mnemos_topology
)
