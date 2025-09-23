cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(zopfleech LANGUAGES C)

# ============================================================================
# Build Configuration Options
# ----------------------------------------------------------------------------
# Standard Options:
#
#   BUILD_SHARED_LIBS : ON | OFF
#     - Builds the zopfleech library as a shared library (.so/.dll).
#     - Defaults to OFF (builds a static library).
#     - The CLI is always linked against static library.
#
# ----------------------------------------------------------------------------
# Project-Specific Options:
#
#   ZOPFLEECH_MIN_CPU : STRING
#     - (x86/x64 only) Sets the minimum CPU instruction set support.
#     - Allows for building a more optimized library for modern CPUs.
#     - Possible values: AVX2, AVX, SSE4.2, SSE2, or an empty string "" for none.
#     - Defaults to "SSE4.2".
#
# ============================================================================

add_subdirectory(src)

# use the standard -DBUILD_SHARED_LIBS=ON convention to build the shared lib of it.
add_subdirectory(src/zopfli)
