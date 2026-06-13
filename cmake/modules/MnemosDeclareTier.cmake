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

    # Register for the whole-graph audit (mnemos_validate_tier_graph). The DEPENDS
    # check above is early, localised feedback; the audit is the authoritative
    # backstop that reads the REAL link graph, so an upward link added via
    # target_link_libraries but omitted from DEPENDS cannot slip through.
    set_property(GLOBAL APPEND PROPERTY MNEMOS_TIERED_TARGETS "${MNEMOS_TIER_NAME}")
endfunction()

# Resolve a link-library entry to a tiered mnemos target name, or "" if it is not
# one (an external dependency, a generator expression, or an untiered target).
function(_mnemos_resolve_tiered_dep entry out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT TARGET "${entry}")
        return()
    endif()
    get_target_property(aliased "${entry}" ALIASED_TARGET)
    if(aliased)
        set(resolved "${aliased}")
    else()
        set(resolved "${entry}")
    endif()
    get_property(tier TARGET "${resolved}" PROPERTY MNEMOS_TIER)
    if(NOT "${tier}" STREQUAL "")
        set(${out_var} "${resolved}" PARENT_SCOPE)
    endif()
endfunction()

# Whole-graph tier audit (ARCH-001 / INV-ARCH-001). After every target is declared,
# walk each tiered target's actual LINK_LIBRARIES + INTERFACE_LINK_LIBRARIES and
# fail if any resolves to a higher-tier mnemos target. Unlike the per-call DEPENDS
# check, this reads the real link graph, so it catches an upward dependency even
# when it was never listed in mnemos_declare_tier(... DEPENDS ...).
function(mnemos_validate_tier_graph)
    get_property(targets GLOBAL PROPERTY MNEMOS_TIERED_TARGETS)
    set(violations "")
    foreach(target IN LISTS targets)
        get_property(target_tier TARGET "${target}" PROPERTY MNEMOS_TIER)
        set(deps "")
        foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(value "${target}" ${prop})
            if(value)
                list(APPEND deps ${value})
            endif()
        endforeach()
        if(deps)
            list(REMOVE_DUPLICATES deps)
        endif()
        foreach(dep IN LISTS deps)
            _mnemos_resolve_tiered_dep("${dep}" resolved)
            if(resolved)
                get_property(dep_tier TARGET "${resolved}" PROPERTY MNEMOS_TIER)
                if(dep_tier GREATER target_tier)
                    list(APPEND violations
                        "  ${target} (tier ${target_tier}) links ${resolved} (tier ${dep_tier})")
                endif()
            endif()
        endforeach()
    endforeach()
    if(violations)
        list(SORT violations)
        list(JOIN violations "\n" report)
        message(
            FATAL_ERROR
            "ARCH-001 tier violation: a lower tier links a higher tier (dependency "
            "direction must be downward). Offending links:\n${report}"
        )
    endif()
endfunction()
