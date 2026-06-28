include(MnemosFetchSDL3)
mnemos_fetch_sdl3()

# Per-system adapters that bridge a manifest to the frontend_sdk::player_system
# interface. Each adapter is its own concrete library so the player exec can
# pick which systems it supports, and so adapters can be unit-tested in
# isolation. Cross-system primitives (video_region, market) live in
# `chips/shared/region.hpp`; system-specific cart-header parsers live in each
# manifest. Currently wired: Genesis, SMS, C64, Sega CD, 32X, M14, M15, M27, M47, M52, M57, M58,
# M62, M63, travrusa, M72, M75, M81, M82, M84, M90, M92, M107, Taito F2, CPS1, CPS2,
# Spectrum, NES, MSX, Amiga 500.
add_subdirectory(adapters/common)
add_subdirectory(adapters/genesis)
add_subdirectory(adapters/sms)
add_subdirectory(adapters/c64)
add_subdirectory(adapters/segacd)
add_subdirectory(adapters/sega32x)
add_subdirectory(adapters/irem_m14)
add_subdirectory(adapters/irem_m15)
add_subdirectory(adapters/irem_m27)
add_subdirectory(adapters/irem_m47)
add_subdirectory(adapters/irem_m52)
add_subdirectory(adapters/irem_m57)
add_subdirectory(adapters/irem_m58)
add_subdirectory(adapters/irem_m62)
add_subdirectory(adapters/irem_m63)
add_subdirectory(adapters/irem_travrusa)
add_subdirectory(adapters/irem_m72)
add_subdirectory(adapters/irem_m75)
add_subdirectory(adapters/irem_m81)
add_subdirectory(adapters/irem_m82)
add_subdirectory(adapters/irem_m84)
add_subdirectory(adapters/irem_m90)
add_subdirectory(adapters/irem_m92)
add_subdirectory(adapters/irem_m107)
add_subdirectory(adapters/taito_f2)
add_subdirectory(adapters/capcom_cps1)
add_subdirectory(adapters/capcom_cps2)
add_subdirectory(adapters/spectrum)
add_subdirectory(adapters/nes)
add_subdirectory(adapters/msx)
add_subdirectory(adapters/msx2)
add_subdirectory(adapters/amiga500)

# Shared bitmap-font overlay helpers, kept in their own static lib so the
# unit test can link against the same code the player binary uses.
add_library(mnemos_apps_player_text_overlay STATIC text_overlay.cpp)
add_library(mnemos::apps::player::text_overlay ALIAS mnemos_apps_player_text_overlay)
target_include_directories(
    mnemos_apps_player_text_overlay PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)
mnemos_apply_common_target_options(mnemos_apps_player_text_overlay PUBLIC)

add_library(
    mnemos_apps_player_core STATIC
    adapter_link.cpp
    headless_commands.cpp
    system_launch.cpp
)
add_library(mnemos::apps::player::core ALIAS mnemos_apps_player_core)
target_include_directories(
    mnemos_apps_player_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)
target_link_libraries(
    mnemos_apps_player_core
    PUBLIC
        mnemos::frontend_sdk
        mnemos::apps::player::adapters::common
    PRIVATE
        mnemos::apps::player::genesis_adapter
        mnemos::apps::player::sms_adapter
        mnemos::apps::player::c64_adapter
        mnemos::apps::player::segacd_adapter
        mnemos::apps::player::sega32x_adapter
        mnemos::apps::player::irem_m14_adapter
        mnemos::apps::player::irem_m15_adapter
        mnemos::apps::player::irem_m27_adapter
        mnemos::apps::player::irem_m47_adapter
        mnemos::apps::player::irem_m52_adapter
        mnemos::apps::player::irem_m57_adapter
        mnemos::apps::player::irem_m58_adapter
        mnemos::apps::player::irem_m62_adapter
        mnemos::apps::player::irem_m63_adapter
        mnemos::apps::player::irem_travrusa_adapter
        mnemos::apps::player::irem_m72_adapter
        mnemos::apps::player::irem_m75_adapter
        mnemos::apps::player::irem_m81_adapter
        mnemos::apps::player::irem_m82_adapter
        mnemos::apps::player::irem_m84_adapter
        mnemos::apps::player::irem_m90_adapter
        mnemos::apps::player::irem_m92_adapter
        mnemos::apps::player::irem_m107_adapter
        mnemos::apps::player::taito_f2_adapter
        mnemos::apps::player::capcom_cps1_adapter
        mnemos::apps::player::capcom_cps2_adapter
        mnemos::apps::player::spectrum_adapter
        mnemos::apps::player::nes_adapter
        mnemos::apps::player::msx_adapter
        mnemos::apps::player::msx2_adapter
        mnemos::apps::player::amiga500_adapter
        mnemos::debug::dump
)
mnemos_apply_common_target_options(mnemos_apps_player_core PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_core
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_apps_player_adapters_common
        mnemos_debug_dump
)

add_executable(mnemos_player main.cpp)

target_link_libraries(
    mnemos_player
    PRIVATE
        SDL3::SDL3-static
        mnemos::apps::player::core
        mnemos::apps::player::text_overlay
)

# SDL3 headers trip /W4 /WX in some configs; mark them SYSTEM so warnings in
# third-party code do not fail our build.
get_target_property(_sdl3_include_dirs SDL3::SDL3-static INTERFACE_INCLUDE_DIRECTORIES)
if(_sdl3_include_dirs)
    target_include_directories(mnemos_player SYSTEM PRIVATE ${_sdl3_include_dirs})
endif()

mnemos_apply_common_target_options(mnemos_player PRIVATE)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_text_overlay_test
        SOURCES tests/text_overlay_test.cpp
        LIBRARIES mnemos::apps::player::text_overlay
    )
    mnemos_add_test(
        mnemos_apps_player_capability_summary_test
        SOURCES tests/capability_summary_test.cpp
        LIBRARIES
            mnemos::debug::dump
            mnemos::apps::player::genesis_adapter
            mnemos::apps::player::sms_adapter
            mnemos::apps::player::c64_adapter
            mnemos::apps::player::segacd_adapter
            mnemos::apps::player::sega32x_adapter
            mnemos::apps::player::irem_m14_adapter
            mnemos::apps::player::irem_m15_adapter
            mnemos::apps::player::irem_m27_adapter
            mnemos::apps::player::irem_m47_adapter
            mnemos::apps::player::irem_m52_adapter
            mnemos::apps::player::irem_m57_adapter
            mnemos::apps::player::irem_m58_adapter
            mnemos::apps::player::irem_m62_adapter
            mnemos::apps::player::irem_m63_adapter
            mnemos::apps::player::irem_travrusa_adapter
            mnemos::apps::player::irem_m72_adapter
            mnemos::apps::player::irem_m75_adapter
            mnemos::apps::player::irem_m81_adapter
            mnemos::apps::player::irem_m82_adapter
            mnemos::apps::player::irem_m84_adapter
            mnemos::apps::player::irem_m90_adapter
            mnemos::apps::player::irem_m92_adapter
            mnemos::apps::player::irem_m107_adapter
            mnemos::apps::player::taito_f2_adapter
            mnemos::apps::player::capcom_cps1_adapter
            mnemos::apps::player::capcom_cps2_adapter
            mnemos::apps::player::msx_adapter
            mnemos::apps::player::amiga500_adapter
    )
endif()
