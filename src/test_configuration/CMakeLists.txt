# src/test_configuration/CMakeLists.txt

add_library(test_configuration INTERFACE)

target_sources(test_configuration
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/test_configuration.h
)

target_include_directories(test_configuration
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_configuration
    INTERFACE
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
