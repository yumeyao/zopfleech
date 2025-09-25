cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(libzopfleech LANGUAGES C)

# enables ffast-math on GCC/clang. This won't go out the project's scope.
foreach(var CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    if(${var} MATCHES "-O(2|3)")
        string(REGEX REPLACE "-O(2|3)" "-Ofast" ${var} "${${var}}")
    endif()
endforeach()

set(ZOPFLI_SOURCES
    blocksplitter.c
    deflate.c
    katajainen.c
    lz77.c
    squeeze.c
    util.c
    zlib_container.c
    gzip_container.c
    zopfli_lib.c
    zopfli_gz.c
    zopfli_io.c
    LzFind.c

    blocksplitter.h
    deflate.h
    katajainen.h
    lz77.h
    squeeze.h
    util.h
    match.h
    zlib_container.h
    gzip_container.h
    zopfli.h
    zopfli_lib.h
    LzFind.h
)
set(ZOPFLI_PUBLIC_HEADERS
    deflate.h
    zlib_container.h
    gzip_container.h
    zopfli.h
    zopfli_lib.h
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86|x86")
    set(ZOPFLEECH_MIN_CPU "SSE4.2" CACHE STRING "Minimum x86 CPU support (AVX2, AVX, SSE4.2, SSE2, or empty)")
    string(TOUPPER "${ZOPFLEECH_MIN_CPU}" MIN_CPU_UPPER)
    if(NOT ";${MIN_CPU_UPPER};" MATCHES ";(AVX2|AVX|SSE4.2|SSE2|);")
        message(FATAL_ERROR "Invalid value for ZOPFLEECH_MIN_CPU: '${ZOPFLEECH_MIN_CPU}'.")
    endif()
endif()

function(zopfleech_add_library TYPE)
    string(TOLOWER ${TYPE} type_lower)
    set(target_name "zopfli_${type_lower}")

    if(TARGET ${target_name})
        return()
    endif()

    set(prefixed_sources "")
    foreach(f ${ZOPFLI_SOURCES})
        list(APPEND prefixed_sources "${CMAKE_CURRENT_LIST_DIR}/${f}")
    endforeach()

    add_library(${target_name} ${TYPE} ${prefixed_sources})
    add_library(zopfli::${target_name} ALIAS ${target_name})

    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    if(NOT MSVC)
        target_compile_options(${target_name} PRIVATE -Wall -Wno-sign-compare -Wno-unused)
    endif()

    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        if(${TYPE} STREQUAL "STATIC")
            target_link_libraries(${target_name} PUBLIC ${MATH_LIBRARY})
        else()
            target_link_libraries(${target_name} PRIVATE ${MATH_LIBRARY})
        endif()
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86|x86" AND ZOPFLEECH_MIN_CPU)
        set(ARCH_FLAG_AVX2 "-mavx2")
        set(ARCH_FLAG_AVX "-mavx")
        set(ARCH_FLAG_SSE42 "-msse4.2")
        set(ARCH_FLAG_SSE2 "-msse2")
        if (MSVC)
            set(ARCH_FLAG_AVX2 "/arch:AVX2")
            set(ARCH_FLAG_AVX "/arch:AVX")
            set(ARCH_FLAG_SSE42 "\${ARCH_FLAG_SSE2}") # The "reference", fallback to sse2
            set(ARCH_FLAG_SSE2 "/arch:SSE2")
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            set(ARCH_FLAG_SSE2 "") # baseline, redundant on x64
        endif()
        string(TOUPPER "${ZOPFLEECH_MIN_CPU}" MIN_CPU_UPPER)
        string(REPLACE "." "" MIN_CPU_UPPER ${MIN_CPU_UPPER}) # "SSE4.2" -> "SSE42"
        string(CONFIGURE "${ARCH_FLAG_${MIN_CPU_UPPER}}" ARCH_FLAG_X86) # expand the "refrence", if any
        target_compile_options(${target_name} PRIVATE ${ARCH_FLAG_X86})
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(ARCH_FLAG_ARM64 "-march=armv8-a+crc")
        if (MSVC)
            set(ARCH_FLAG_ARM64 "/arch:armv8.1")
        endif()
        target_compile_options(${target_name} PRIVATE -march=armv8-a+crc)
    endif()
endfunction()

if(DEFINED ZOPFLEECH_LIB_TYPE)
    string(TOUPPER "${ZOPFLEECH_LIB_TYPE}" lib_type_to_build)
    if(NOT (lib_type_to_build STREQUAL "STATIC" OR lib_type_to_build STREQUAL "SHARED"))
        message(FATAL_ERROR "Invalid library type '${ZOPFLEECH_LIB_TYPE}'. Must be STATIC or SHARED.")
    endif()
elseif(DEFINED BUILD_SHARED_LIBS AND BUILD_SHARED_LIBS)
    set(lib_type_to_build "SHARED")
else()
    set(lib_type_to_build "STATIC")
endif()
zopfleech_add_library(${lib_type_to_build})
