# src/execution_mid_level/exec_digital_input/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXEC_DIGITAL_INPUT_SOURCES
    exec_digital_input.c
)

set(EXEC_DIGITAL_INPUT_HEADERS
    exec_digital_input.h
)

add_library(exec_digital_input STATIC
    ${EXEC_DIGITAL_INPUT_SOURCES}
    ${EXEC_DIGITAL_INPUT_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(exec_digital_input
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exec_digital_input
    PUBLIC
        global_config
        rtos
        cubeide_hal
        hw_gpio
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXEC_DIGITAL_INPUT_ENABLE_TESTS "Build tests for exec_digital_input module" ON)

if(EXEC_DIGITAL_INPUT_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(exec_DigitalInput_Tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exec_digital_input.cpp
    )

    target_link_libraries(exec_DigitalInput_Tests
        PRIVATE
            exec_digital_input
            gtest
            gtest_main
            gmock
    )

    target_include_directories(exec_DigitalInput_Tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME exec_DigitalInput_Tests COMMAND exec_DigitalInput_Tests)

endif()
