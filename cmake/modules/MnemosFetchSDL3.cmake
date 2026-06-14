include(FetchContent)

# Fetch SDL3 (windowing + GPU + audio + input). Pinned to a stable 3.2 tag so
# CI is reproducible; bump the tag deliberately, not opportunistically.
function(mnemos_fetch_sdl3)
    if(TARGET SDL3::SDL3)
        return()
    endif()

    # Static SDL keeps the player tool a single .exe and matches the rest of
    # the build (no DLL chasing). Disable the shared library + tests/examples.
    set(SDL_STATIC ON CACHE INTERNAL "")
    set(SDL_SHARED OFF CACHE INTERNAL "")
    set(SDL_TEST_LIBRARY OFF CACHE INTERNAL "")
    set(SDL_TESTS OFF CACHE INTERNAL "")
    set(SDL_INSTALL OFF CACHE INTERNAL "")
    set(SDL_DISABLE_INSTALL ON CACHE INTERNAL "")
    set(SDL_DISABLE_INSTALL_DOCS ON CACHE INTERNAL "")
    set(SDL_DISABLE_INSTALL_CPACK ON CACHE INTERNAL "")

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 535d80badefc83c5c527ec5748f2a20d6a9310fe # release-3.2.0
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(SDL3)
endfunction()
