include(MnemosFetchSDL3)
mnemos_fetch_sdl3()

# Per-system adapters that bridge a manifest to the frontend_sdk::player_system
# interface. Each adapter is its own concrete library so the player exec can
# pick which systems it supports, and so adapters can be unit-tested in
# isolation. Cross-system primitives (video_region, market) live in
# `chips/shared/region.hpp`; system-specific cart-header parsers live in each
# manifest. Currently wired: Genesis, SMS. C64, 32X, Sega CD, SNES, NES slot in.
add_subdirectory(adapters/genesis)
add_subdirectory(adapters/sms)

add_executable(mnemos_player main.cpp)

target_link_libraries(
    mnemos_player
    PRIVATE
        SDL3::SDL3-static
        mnemos::frontend_sdk
        mnemos::apps::player::genesis_adapter
        mnemos::apps::player::sms_adapter
)

# SDL3 headers trip /W4 /WX in some configs; mark them SYSTEM so warnings in
# third-party code do not fail our build.
get_target_property(_sdl3_include_dirs SDL3::SDL3-static INTERFACE_INCLUDE_DIRECTORIES)
if(_sdl3_include_dirs)
    target_include_directories(mnemos_player SYSTEM PRIVATE ${_sdl3_include_dirs})
endif()

mnemos_apply_common_target_options(mnemos_player PRIVATE)
