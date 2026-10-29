include_guard(GLOBAL)

function(lumen_import_android_v8_prebuilt)
    set(v8_android_api "${ANDROID_PLATFORM_LEVEL}")
    if(NOT v8_android_api)
        set(v8_android_api "${CMAKE_SYSTEM_VERSION}")
    endif()
    if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a" OR
            NOT v8_android_api MATCHES "^[0-9]+$" OR v8_android_api LESS 29)
        message(FATAL_ERROR "Pinned prebuilt V8 requires Android API29+ and ABI arm64-v8a")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
            CMAKE_CXX_COMPILER_VERSION VERSION_LESS 24 OR
            NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 25)
        message(FATAL_ERROR "Prebuilt V8 consumers require the verified Clang 24 compiler")
    endif()
    if(NOT ANDROID_STL STREQUAL "none")
        message(FATAL_ERROR "Prebuilt V8 supplies custom libc++; configure ANDROID_STL=none")
    endif()
    file(READ "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../android/native-deps/v8-prebuilt.json" manifest)
    string(JSON expected_compiler GET "${manifest}" compiler_sha256)
    file(SHA256 "${CMAKE_CXX_COMPILER}" compiler_digest)
    if(NOT compiler_digest STREQUAL expected_compiler)
        message(FATAL_ERROR "Prebuilt V8 compiler checksum differs from the verified toolchain")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/android/v8_prebuilt.py"
        --sdk "${LUMEN_V8_PREBUILT_DIR}" --abi "${ANDROID_ABI}" --api "${v8_android_api}"
        RESULT_VARIABLE verification OUTPUT_VARIABLE verified ERROR_VARIABLE failure)
    if(NOT verification EQUAL 0)
        message(FATAL_ERROR "Prebuilt V8 validation failed: ${failure}")
    endif()
    message(STATUS "Verified prebuilt V8: ${verified}")
    foreach(runtime libatomic.a libunwind.a)
        if(NOT EXISTS "${lumen_runtime_dir}/${runtime}")
            message(FATAL_ERROR "Use android-v8.toolchain.cmake with the matching NDK runtimes")
        endif()
    endforeach()
    add_library(lumen_android_temporal STATIC IMPORTED GLOBAL)
    set_target_properties(lumen_android_temporal PROPERTIES
        IMPORTED_LOCATION "${LUMEN_V8_PREBUILT_DIR}/libtemporal_support.a")
    add_library(lumen_android_v8 STATIC IMPORTED GLOBAL)
    set_target_properties(lumen_android_v8 PROPERTIES
        IMPORTED_LOCATION "${LUMEN_V8_PREBUILT_DIR}/librusty_v8.a"
        IMPORTED_LINK_INTERFACE_LANGUAGES CXX
        LUMEN_ICU_INCLUDE_DIR "${LUMEN_V8_PREBUILT_DIR}/icu")
    target_include_directories(lumen_android_v8 SYSTEM INTERFACE
        "${LUMEN_V8_PREBUILT_DIR}/include" "${LUMEN_V8_PREBUILT_DIR}/icu"
        "$<$<COMPILE_LANGUAGE:CXX>:${LUMEN_V8_PREBUILT_DIR}/libcxx/config>"
        "$<$<COMPILE_LANGUAGE:CXX>:${LUMEN_V8_PREBUILT_DIR}/libcxx/include>"
        "$<$<COMPILE_LANGUAGE:CXX>:${LUMEN_V8_PREBUILT_DIR}/libcxxabi/include>")
    target_compile_definitions(lumen_android_v8 INTERFACE
        U_STATIC_IMPLEMENTATION V8_COMPRESS_POINTERS V8_COMPRESS_POINTERS_IN_SHARED_CAGE
        V8_31BIT_SMIS_ON_64BIT_ARCH V8_ENABLE_SANDBOX V8_EXTERNAL_CODE_SPACE
        V8_STATIC_ROOTS V8_ENABLE_WEBASSEMBLY V8_HAVE_TARGET_OS V8_TARGET_OS_ANDROID
        V8_TARGET_ARCH_ARM64 V8_ARRAY_BUFFER_INTERNAL_FIELD_COUNT=2
        V8_ARRAY_BUFFER_VIEW_INTERNAL_FIELD_COUNT=2 V8_PROMISE_INTERNAL_FIELD_COUNT=1
        V8_USE_DEFAULT_HASHER_SECRET=false V8_DEPRECATION_WARNINGS
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>"
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS>"
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS>"
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_INSTRUMENTED_WITH_ASAN=0>")
    target_compile_options(lumen_android_v8 INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-experimental-relative-c++-abi-vtables>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>")
    target_link_options(lumen_android_v8 INTERFACE
        -nostdlib++ -unwindlib=none "-Wl,--exclude-libs,ALL" "-Wl,-z,max-page-size=16384"
        "-Wl,-z,defs" "-Wl,--threads=2")
    target_link_libraries(lumen_android_v8 INTERFACE lumen_android_temporal
        "${lumen_runtime_dir}/libunwind.a" "${lumen_runtime_dir}/libatomic.a" android log dl m)
endfunction()
