add_subdirectory(common)
add_subdirectory(cpu)
add_subdirectory(video)
add_subdirectory(mapper)
add_subdirectory(bus_controller)
add_subdirectory(audio)

add_library(mnemos_chips INTERFACE)
add_library(mnemos::chips ALIAS mnemos_chips)

target_link_libraries(mnemos_chips INTERFACE mnemos::foundation)

mnemos_apply_common_target_options(mnemos_chips INTERFACE)
mnemos_declare_tier(NAME mnemos_chips TIER 2 DEPENDS mnemos_foundation)
