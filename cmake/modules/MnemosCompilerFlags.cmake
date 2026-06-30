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
            # Partial aggregate-init (only setting the fields a test/call site
            # cares about, leaving the rest value-initialized) is this
            # codebase's deliberate idiom for option/params structs, not a bug.
            -Wno-missing-field-initializers
        )
    endif()
endfunction()

function(mnemos_apply_coverage_options target visibility)
    if(NOT MNEMOS_ENABLE_COVERAGE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MNEMOS_ENABLE_COVERAGE requires a Clang compiler with llvm-cov.")
    endif()

    target_compile_options("${target}" ${visibility} -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options("${target}" ${visibility} -fprofile-instr-generate -fcoverage-mapping)
endfunction()

function(mnemos_apply_common_target_options target visibility)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "mnemos_apply_common_target_options target does not exist: ${target}")
    endif()

    target_compile_features("${target}" ${visibility} cxx_std_23)
    mnemos_apply_project_warnings("${target}" "${visibility}")
    mnemos_apply_coverage_options("${target}" "${visibility}")
endfunction()
