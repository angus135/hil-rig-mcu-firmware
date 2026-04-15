# src/hardware_low_level/hw_uart/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_UART_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/hw_uart_dut.c
)

set(HW_UART_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/hw_uart_dut.h
)

add_library(hw_uart STATIC
    ${HW_UART_SOURCES}
    ${HW_UART_HEADERS}
)

# Public include path for users of hw_uart.h
# Private include path for Cube-generated headers such as usart.h
target_include_directories(hw_uart
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/../f446ze_cubeide_project/Core/Inc
)

target_link_libraries(hw_uart
    PUBLIC
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module
# -----------------------------

option(HW_UART_ENABLE_TESTS "Build tests for hw_uart module" ON)

if(HW_UART_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_uart_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_uart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw_uart_dut.c
    )

    target_compile_definitions(hw_uart_tests
        PRIVATE
            TEST_BUILD
    )

    target_include_directories(hw_uart_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(hw_uart_tests
        PRIVATE
            global_config
            rtos
            gtest
            gtest_main
            gmock
    )

    add_test(NAME hw_uart_tests COMMAND hw_uart_tests)

endif()