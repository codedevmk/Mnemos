function(mnemos_apply_project_warnings target visibility)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "mnemos_apply_project_warnings target does not exist: ${target}")
    endif()

    if(MSVC)
        target_compile_options(
            "${target}" ${visibility}
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
        )
    else()
        target_compile_options(
            "${target}" ${visibility}
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
    endif()
endfunction()

function(mnemos_apply_common_target_options target visibility)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "mnemos_apply_common_target_options target does not exist: ${target}")
    endif()

    target_compile_features("${target}" ${visibility} cxx_std_23)
    mnemos_apply_project_warnings("${target}" "${visibility}")
endfunction()
