function(mnemos_declare_tier)
    set(options)
    set(one_value_args NAME TIER)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(MNEMOS_TIER "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT MNEMOS_TIER_NAME)
        message(FATAL_ERROR "mnemos_declare_tier requires NAME")
    endif()

    if(NOT MNEMOS_TIER_TIER)
        message(FATAL_ERROR "mnemos_declare_tier requires TIER")
    endif()

    if(NOT TARGET "${MNEMOS_TIER_NAME}")
        message(FATAL_ERROR "mnemos_declare_tier target does not exist: ${MNEMOS_TIER_NAME}")
    endif()

    foreach(dependency IN LISTS MNEMOS_TIER_DEPENDS)
        if(NOT TARGET "${dependency}")
            message(
                FATAL_ERROR
                "Tier ${MNEMOS_TIER_NAME} depends on unknown target ${dependency}"
            )
        endif()

        get_property(dependency_tier TARGET "${dependency}" PROPERTY MNEMOS_TIER)
        if("${dependency_tier}" STREQUAL "")
            message(
                FATAL_ERROR
                "Tier dependency ${dependency} has no MNEMOS_TIER property"
            )
        endif()

        # Tier rule: dependencies must point at the same tier or below. Strict
        # downward (lower tier only) was too restrictive -- siblings within an
        # adapter / app tier compose naturally (e.g. apps/player/adapters/common
        # is a sibling helper of apps/player/adapters/genesis), and CMake's own
        # topological sort guarantees no build-time cycle. Upward deps remain
        # forbidden because those are the real architectural smell.
        if(dependency_tier GREATER MNEMOS_TIER_TIER)
            message(
                FATAL_ERROR
                "Tier ${MNEMOS_TIER_NAME} (${MNEMOS_TIER_TIER}) may not depend on "
                "${dependency} (${dependency_tier}); dependencies must not point at higher tiers"
            )
        endif()
    endforeach()

    set_property(TARGET "${MNEMOS_TIER_NAME}" PROPERTY MNEMOS_TIER "${MNEMOS_TIER_TIER}")
endfunction()
