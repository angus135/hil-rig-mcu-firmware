# src/execution_mid_level/exec_can/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXEC_CAN_SOURCES
    exec_can.c
)

set(EXEC_CAN_HEADERS
    exec_can.h
)

add_library(exec_can STATIC
    ${EXEC_CAN_SOURCES}
    ${EXEC_CAN_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(exec_can
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exec_can
    PUBLIC
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXEC_CAN_ENABLE_TESTS "Build tests for exec_can module" ON)

if(EXEC_CAN_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(exec_can_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exec_can.cpp
    )

    target_link_libraries(exec_can_tests
        PRIVATE
            exec_can
            gtest
            gtest_main
            gmock
    )

    target_include_directories(exec_can_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME exec_can_tests COMMAND exec_can_tests)

endif()
