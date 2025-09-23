cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(zopfleech_cli LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)

# use include to create the static lib for us while allowing the user builds a shared lib with add_subdirectory().
set(ZOPFLEECH_LIB_TYPE STATIC)
include(zopfli/CMakeLists.txt)

find_package(ZLIB REQUIRED)

add_executable(ect
	main.cpp
	gztools.cpp
	support.cpp
	# Add headers so they get added to things like Xcode projects
	gztools.h
	main.h
	pngusr.h
	support.h)

add_executable(zopgz zopgz.c ungzlib.c)

# Link explicitly to the static target we just created.
target_link_libraries(zopgz PRIVATE zopfli::zopfli_static ZLIB::ZLIB)
target_link_libraries(ect PRIVATE zopfli::zopfli_static ZLIB::ZLIB)

if(NOT MSVC)
    target_compile_options(ect PRIVATE -Wall -Wno-sign-compare -Wno-unused)
    target_compile_options(zopgz PRIVATE -Wall -Wno-sign-compare -Wno-unused)
endif()
