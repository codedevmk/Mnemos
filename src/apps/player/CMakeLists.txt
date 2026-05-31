include(MnemosFetchSDL3)
mnemos_fetch_sdl3()

# Per-system adapters that bridge a manifest to the frontend_sdk::player_system
# interface. Each adapter is its own concrete library so the player exec can
# pick which systems it supports, and so adapters can be unit-tested in
# isolation. Cross-system primitives (video_region, market) live in
# `chips/shared/region.hpp`; system-specific cart-header parsers live in each
# manifest. Currently wired: Genesis, SMS. C64, 32X, Sega CD, SNES, NES slot in.
add_subdirectory(adapters/common)
add_subdirectory(adapters/genesis)
add_subdirectory(adapters/sms)

# Shared bitmap-font overlay helpers, kept in their own static lib so the
# unit test can link against the same code the player binary uses.
add_library(mnemos_apps_player_text_overlay STATIC text_overlay.cpp)
add_library(mnemos::apps::player::text_overlay ALIAS mnemos_apps_player_text_overlay)
target_include_directories(
    mnemos_apps_player_text_overlay PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)
mnemos_apply_common_target_options(mnemos_apps_player_text_overlay PUBLIC)

# System-agnostic debug-dump helpers (--screenshot path). Pulled into its own
# static lib so the unit test exercises the same code the player binary uses.
add_library(mnemos_apps_player_debug_dump STATIC debug_dump.cpp)
add_library(mnemos::apps::player::debug_dump ALIAS mnemos_apps_player_debug_dump)
target_include_directories(
    mnemos_apps_player_debug_dump PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)
target_link_libraries(
    mnemos_apps_player_debug_dump
    PUBLIC
        mnemos::frontend_sdk
        mnemos::chips::shared
    PRIVATE
        mnemos::io
        mnemos::graphics::images
)
mnemos_apply_common_target_options(mnemos_apps_player_debug_dump PUBLIC)

add_executable(mnemos_player main.cpp)

target_link_libraries(
    mnemos_player
    PRIVATE
        SDL3::SDL3-static
        mnemos::frontend_sdk
        mnemos::apps::player::adapters::common
        mnemos::apps::player::genesis_adapter
        mnemos::apps::player::sms_adapter
        mnemos::apps::player::text_overlay
        mnemos::apps::player::debug_dump
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
        mnemos_apps_player_debug_dump_test
        SOURCES tests/debug_dump_test.cpp
        LIBRARIES mnemos::apps::player::debug_dump
    )
endif()
