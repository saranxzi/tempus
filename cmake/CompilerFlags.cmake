# cmake/CompilerFlags.cmake
# Reusable function to apply size-optimized release flags to any target.

include(CheckIPOSupported)

function(tempus_apply_release_flags target)
    # --- LTO (Link-Time Optimization) ---
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(STATUS "LTO not supported or disabled for ${target}")
    endif()

    # --- Compiler-specific size flags ---
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:/O1 /Gy /GR->
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections -fno-rtti>
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:-Wl,--gc-sections -s -static>
        )
    endif()
endfunction()
