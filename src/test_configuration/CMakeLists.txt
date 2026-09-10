# src/test_configuration/CMakeLists.txt

set(TEST_CONFIGURATION_SOURCES
    test_configuration.c
)

set(TEST_CONFIGURATION_HEADERS
    test_configuration.h
)

add_library(test_configuration STATIC
    ${TEST_CONFIGURATION_SOURCES}
    ${TEST_CONFIGURATION_HEADERS}
)

target_include_directories(test_configuration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_configuration
    PUBLIC
        project_warnings
        rtos
        exec_analogue_input
        exec_analogue_output
        exec_can
        exec_digital_input
        exec_digital_output
        exec_i2c
        exec_pwm_capture
        exec_pwm_gen
        exec_spi
        exec_uart
)
