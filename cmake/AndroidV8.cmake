include_guard(GLOBAL)

function(lumen_require_v8_argument name expected)
    file(STRINGS "${LUMEN_V8_BUILD_DIR}/args.gn" settings REGEX "^[ \t]*${name}[ \t]*=")
    list(LENGTH settings count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "V8 args.gn must explicitly set ${name}=${expected} once")
    endif()
    string(REGEX REPLACE "^[^=]+=[ \t]*" "" actual "${settings}")
    string(REGEX REPLACE "[ \t]*#.*$" "" actual "${actual}")
    string(STRIP "${actual}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "V8 args.gn requires ${name}=${expected}, found ${actual}")
    endif()
endfunction()

function(lumen_import_android_v8)
    if(NOT ANDROID)
        message(FATAL_ERROR "Android V8 cannot be imported for a host build")
    endif()
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(v8_cpu arm64)
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(v8_cpu x64)
    else()
        message(FATAL_ERROR "Android V8 supports arm64-v8a or x86_64")
    endif()
    set(LUMEN_V8_DIR "${LUMEN_V8_DIR}" CACHE PATH "Pinned V8 source and custom C++ runtime checkout")
    set(LUMEN_V8_BUILD_DIR "${LUMEN_V8_DIR}/out/android-${v8_cpu}" CACHE PATH "Matching Android V8 GN output")
    foreach(required
            "${LUMEN_V8_DIR}/include/v8.h"
            "${LUMEN_V8_DIR}/third_party/libc++/src/include/memory"
            "${LUMEN_V8_DIR}/third_party/libc++abi/src/include/cxxabi.h"
            "${LUMEN_V8_DIR}/buildtools/third_party/libc++/__config_site"
            "${LUMEN_V8_DIR}/third_party/icu/source/common/unicode/uidna.h"
            "${LUMEN_V8_BUILD_DIR}/args.gn"
            "${LUMEN_V8_BUILD_DIR}/gen/include/v8-gn.h")
        if(NOT EXISTS "${required}")
            message(FATAL_ERROR "Missing Android V8 input: ${required}")
        endif()
    endforeach()
    file(REAL_PATH "${LUMEN_V8_DIR}/third_party/llvm-build/Release+Asserts/bin/clang++" expected_compiler)
    file(REAL_PATH "${CMAKE_CXX_COMPILER}" actual_compiler)
    if(NOT actual_compiler STREQUAL expected_compiler OR
            CMAKE_CXX_COMPILER_VERSION VERSION_LESS 24 OR
            NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 25)
        message(FATAL_ERROR "Use cmake/android-v8.toolchain.cmake with V8's pinned Clang 24")
    endif()
    lumen_require_v8_argument(target_os "\"android\"")
    lumen_require_v8_argument(target_cpu "\"${v8_cpu}\"")
    lumen_require_v8_argument(android_ndk_api_level 28)
    foreach(name is_debug is_component_build v8_use_external_startup_data
            icu_use_data_file icu_copy_icudata_to_root_build_dir use_custom_libunwind)
        lumen_require_v8_argument(${name} false)
    endforeach()
    foreach(name v8_monolithic v8_monolithic_for_shared_library
            v8_generate_external_defines_header v8_enable_i18n_support
            v8_enable_pointer_compression v8_enable_webassembly use_custom_libcxx)
        lumen_require_v8_argument(${name} true)
    endforeach()
    add_library(lumen_android_v8 STATIC IMPORTED GLOBAL)
    set_target_properties(lumen_android_v8 PROPERTIES
        IMPORTED_LOCATION "${LUMEN_V8_BUILD_DIR}/obj/libv8_monolith.a"
        IMPORTED_LINK_INTERFACE_LANGUAGES CXX)
    target_include_directories(lumen_android_v8 SYSTEM INTERFACE
        "${LUMEN_V8_DIR}/include"
        "${LUMEN_V8_BUILD_DIR}/gen/include"
        "${LUMEN_V8_DIR}/third_party/icu/source/common"
        "$<$<COMPILE_LANGUAGE:CXX>:${LUMEN_V8_DIR}/buildtools/third_party/libc++>"
        "$<$<COMPILE_LANGUAGE:CXX>:${LUMEN_V8_DIR}/third_party/libc++/src/include>"
        "$<$<COMPILE_LANGUAGE:CXX>:${LUMEN_V8_DIR}/third_party/libc++abi/src/include>")
    target_compile_definitions(lumen_android_v8 INTERFACE
        V8_GN_HEADER U_STATIC_IMPLEMENTATION
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>"
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS>"
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS>"
        "$<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_INSTRUMENTED_WITH_ASAN=0>")
    target_compile_options(lumen_android_v8 INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fexperimental-relative-c++-abi-vtables>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>")
    target_link_options(lumen_android_v8 INTERFACE "-Wl,--exclude-libs,ALL" "-Wl,-z,max-page-size=16384")
    target_link_libraries(lumen_android_v8 INTERFACE android log dl m)
endfunction()
