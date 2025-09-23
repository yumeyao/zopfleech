cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(zopfleech_cli LANGUAGES C)

# use include to create the static lib for us while allowing the user builds a shared lib with add_subdirectory().
set(ZOPFLEECH_LIB_TYPE STATIC)
include(zopfli/CMakeLists.txt)

find_package(ZLIB REQUIRED)

add_executable(zopgz
    zopgz.c
    ungzlib.c

    ungzlib.h
    )

# Link explicitly to the static target we just created.
target_link_libraries(zopgz PRIVATE zopfli::zopfli_static ZLIB::ZLIB)

if(NOT MSVC)
    target_compile_options(zopgz PRIVATE -Wall -Wno-sign-compare -Wno-unused)
endif()
