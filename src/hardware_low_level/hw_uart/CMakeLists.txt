# src/hardware_low_level/hw_uart/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_UART_SOURCES
    hw_uart_dut.c
    hw_uart_console.c
)

set(HW_UART_HEADERS
    hw_uart_dut.h
    hw_uart_console.h
)

add_library(hw_uart STATIC
    ${HW_UART_SOURCES}
    ${HW_UART_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_uart
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_uart
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HW_UART_ENABLE_TESTS "Build tests for hw_uart" ON)

if(HW_UART_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_uart_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_uart.cpp
    )

    target_link_libraries(hw_uart_tests
        PRIVATE
            project_warnings
            hw_uart
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_uart_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_uart_tests COMMAND hw_uart_tests)

endif()

