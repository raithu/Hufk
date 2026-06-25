cmake_minimum_required(VERSION 3.16)
project(huffman VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Build type defaults to Release if not specified
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif()

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
add_compile_options(
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wshadow>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wconversion>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(huffman src/huffman.cpp)

target_compile_options(huffman PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O2>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-g -fsanitize=address,undefined>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:RelWithDebInfo>>:-O2 -g>
)

target_link_options(huffman PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-fsanitize=address,undefined>
)

target_link_libraries(huffman PRIVATE m)

# ---------------------------------------------------------------------------
# Install target (optional: `cmake --install build/`)
# ---------------------------------------------------------------------------
install(TARGETS huffman RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# CTest round-trip smoke test
# ---------------------------------------------------------------------------
enable_testing()

add_test(
    NAME roundtrip_classic
    COMMAND ${CMAKE_COMMAND}
        -DHUFFMAN=$<TARGET_FILE:huffman>
        -DINPUT=${CMAKE_SOURCE_DIR}/tests/sample.txt
        -P ${CMAKE_SOURCE_DIR}/cmake/RoundTripTest.cmake
)

add_test(
    NAME roundtrip_canonical
    COMMAND ${CMAKE_COMMAND}
        -DHUFFMAN=$<TARGET_FILE:huffman>
        -DINPUT=${CMAKE_SOURCE_DIR}/tests/sample.txt
        -DCANONICAL=1
        -P ${CMAKE_SOURCE_DIR}/cmake/RoundTripTest.cmake
)

set_tests_properties(roundtrip_classic roundtrip_canonical
    PROPERTIES TIMEOUT 30
)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ std    : C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
