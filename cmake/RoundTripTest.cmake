# cmake/RoundTripTest.cmake
# Called by CTest to run a compress -> decompress -> diff cycle.
# Variables expected from add_test():
#   HUFFMAN  - path to the huffman executable
#   INPUT    - path to the sample input file
#   CANONICAL (optional) - if set, passes --canonical flag

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Test input not found: ${INPUT}")
endif()

set(HUFF_OUT "${INPUT}.test.huff")
set(DEC_OUT  "${INPUT}.test.decoded")

if(CANONICAL)
    execute_process(
        COMMAND "${HUFFMAN}" compress "${INPUT}" "${HUFF_OUT}" --canonical
        RESULT_VARIABLE RES
    )
else()
    execute_process(
        COMMAND "${HUFFMAN}" compress "${INPUT}" "${HUFF_OUT}"
        RESULT_VARIABLE RES
    )
endif()

if(NOT RES EQUAL 0)
    message(FATAL_ERROR "Compress step failed (exit code ${RES})")
endif()

execute_process(
    COMMAND "${HUFFMAN}" decompress "${HUFF_OUT}" "${DEC_OUT}"
    RESULT_VARIABLE RES
)

if(NOT RES EQUAL 0)
    file(REMOVE "${HUFF_OUT}" "${DEC_OUT}")
    message(FATAL_ERROR "Decompress step failed (exit code ${RES})")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files "${INPUT}" "${DEC_OUT}"
    RESULT_VARIABLE DIFF
)

file(REMOVE "${HUFF_OUT}" "${DEC_OUT}")

if(NOT DIFF EQUAL 0)
    message(FATAL_ERROR "Round-trip FAILED: decoded output differs from original")
endif()

message(STATUS "Round-trip OK")
