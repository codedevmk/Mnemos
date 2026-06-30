if(NOT TARGET mnemos::apps::player::amiga_adapter)
    message(STATUS "Skipping mnemos_amiga_eval_probe because MNEMOS_BUILD_APPS is OFF")
    return()
endif()

add_executable(mnemos_amiga_eval_probe main.cpp)
target_link_libraries(
    mnemos_amiga_eval_probe
    PRIVATE
        mnemos::apps::player::amiga_adapter
        mnemos::debug::dump
)
mnemos_apply_common_target_options(mnemos_amiga_eval_probe PRIVATE)
