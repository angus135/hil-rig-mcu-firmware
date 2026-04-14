# src/execution_mid_level/exec_uart/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXEC_UART_SOURCES
    exec_uart.c
)

set(EXEC_UART_HEADERS
    exec_uart.h
)

add_library(exec_uart STATIC
    ${EXEC_UART_SOURCES}
    ${EXEC_UART_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(exec_uart
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exec_uart
    PUBLIC
        hw_uart
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXEC_UART_ENABLE_TESTS "Build tests for exec_uart module" ON)

if(EXEC_UART_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(exec_uart_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exec_uart.cpp
    )

    target_link_libraries(exec_uart_tests
        PRIVATE
            exec_uart
            gtest
            gtest_main
            gmock
    )

    target_include_directories(exec_uart_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME exec_uart_tests COMMAND exec_uart_tests)

endif()
