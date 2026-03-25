# src/execution_mid_level/exec_analogue_output/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXEC_ANALOGUE_OUTPUT_SOURCES
    exec_analogue_output.c
)

set(EXEC_ANALOGUE_OUTPUT_HEADERS
    exec_analogue_output.h
)

add_library(exec_analogue_output STATIC
    ${EXEC_ANALOGUE_OUTPUT_SOURCES}
    ${EXEC_ANALOGUE_OUTPUT_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(exec_analogue_output
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exec_analogue_output
    PUBLIC
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXEC_ANALOGUE_OUTPUT_ENABLE_TESTS "Build tests for exec_analogue_output module" ON)

if(EXEC_ANALOGUE_OUTPUT_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(exec_analogue_output_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exec_analogue_output.cpp
    )

    target_link_libraries(exec_analogue_output_tests
        PRIVATE
            exec_analogue_output
            gtest
            gtest_main
            gmock
    )

    target_include_directories(exec_analogue_output_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME exec_analogue_output_tests COMMAND exec_analogue_output_tests)

endif()
