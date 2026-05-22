include(FetchContent)

function(mnemos_fetch_catch2)
    if(TARGET Catch2::Catch2WithMain)
        return()
    endif()

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG 56809e5282f104c5c8b570e7c2996cdc352d94f1
    )

    FetchContent_MakeAvailable(Catch2)
endfunction()

function(mnemos_add_test target)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES LIBRARIES)
    cmake_parse_arguments(MNEMOS_TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT MNEMOS_TEST_SOURCES)
        message(FATAL_ERROR "mnemos_add_test(${target}) requires SOURCES")
    endif()

    mnemos_fetch_catch2()

    add_executable("${target}" ${MNEMOS_TEST_SOURCES})
    target_link_libraries("${target}" PRIVATE Catch2::Catch2WithMain ${MNEMOS_TEST_LIBRARIES})
    mnemos_apply_common_target_options("${target}" PRIVATE)

    add_test(NAME "${target}" COMMAND "${target}")
endfunction()
