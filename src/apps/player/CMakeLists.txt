include(MnemosFetchSDL3)
mnemos_fetch_sdl3()

add_executable(mnemos_player main.cpp)

# SDL3 headers and our own player-tool sources only; the player_system
# interface comes from the frontend_sdk tier and concrete system adapters
# are linked in as their manifests land (Commit 3 onwards).
target_link_libraries(
    mnemos_player
    PRIVATE
        SDL3::SDL3-static
)

# SDL3 headers trip /W4 /WX in some configs; mark them SYSTEM so warnings in
# third-party code do not fail our build.
get_target_property(_sdl3_include_dirs SDL3::SDL3-static INTERFACE_INCLUDE_DIRECTORIES)
if(_sdl3_include_dirs)
    target_include_directories(mnemos_player SYSTEM PRIVATE ${_sdl3_include_dirs})
endif()

mnemos_apply_common_target_options(mnemos_player PRIVATE)
