# src/console/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(CONSOLE_SOURCES
    console.c
    command_handler.c
    subsystem_command_apis/console_spi.c
    subsystem_command_apis/console_uart.c
    subsystem_command_apis/console_logic_expander.c
    subsystem_command_apis/console_I2C.c
    subsystem_command_apis/console_can.c
)

set(CONSOLE_HEADERS
    console.h
    subsystem_command_apis/console_can.h
)

add_library(console STATIC
    ${CONSOLE_SOURCES}
    ${CONSOLE_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(console
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(console
    PUBLIC
    global_config
    project_warnings
    cubeide_hal
    rtos
    exec_uart
    hw_can
    hw_uart
    hw_adc
    hw_spi
    exec_spi
    exec_analogue_output
    execution_manager
    exec_digital_input
    exec_can
    hw_gpio
    hw_i2c
    exec_i2c
    logic_expander
    exec_pwm_capture
    exec_pwm_gen
    hw_pwm_gen
    hw_usb
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------
option(CONSOLE_ENABLE_TESTS "Build tests for console module" ON)

if(CONSOLE_ENABLE_TESTS AND BUILD_TESTING)
    add_executable(console_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_console.cpp
    )

    target_link_libraries(console_tests
        PRIVATE
        project_warnings
        console
        gtest
        gtest_main
        gmock
    )

    target_include_directories(console_tests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME console_tests COMMAND console_tests)

    add_executable(console_can_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/subsystem_command_apis/console_can.c
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_console_can.cpp
    )

    target_link_libraries(console_can_tests
        PRIVATE
        project_warnings
        global_config
        rtos
        gtest
        gtest_main
    )

    target_include_directories(console_can_tests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/subsystem_command_apis
        ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_can
        ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_can
    )

    add_test(NAME console_can_tests COMMAND console_can_tests)
endif()
