# src/hardware/hw_uart/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_CAN_SOURCES
    hw_can.c
)

set(HW_CAN_HEADERS
    hw_can.h
)

add_library(hw_can STATIC
    ${HW_CAN_SOURCES}
    ${HW_CAN_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_can
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_can
    PUBLIC
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HW_CAN_ENABLE_TESTS "Build tests for hw_can module" ON)

if(HW_CAN_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_can_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_can.cpp
    )

    target_link_libraries(hw_can_tests
        PRIVATE
            hw_can
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_can_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_can_tests COMMAND hw_can_tests)

endif()
