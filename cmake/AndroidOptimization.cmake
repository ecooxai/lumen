option(LUMEN_ANDROID_OPTIMIZED_DEBUG "Optimize Android debug APKs while retaining symbols and assertions" ON)

function(lumen_android_optimization)
    if(ANDROID AND LUMEN_ANDROID_OPTIMIZED_DEBUG)
        foreach(target IN LISTS ARGN)
            target_compile_options(${target} PRIVATE "$<$<CONFIG:Debug>:-O2>")
        endforeach()
    endif()
endfunction()
