# src/host_interface/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HOST_INTERFACE_SOURCES
    host_interface.c
    host_process_message.c
    instruction_message_handler.c
    result_message_producer.c
    variable_instruction_message_handler.c
    variable_result_message_producer.c
    config_message_handler.c
)

set(HOST_INTERFACE_HEADERS
    host_interface.h
    host_process_message.h
    instruction_message_handler.h
    result_message_producer.h
    variable_instruction_message_handler.h
    variable_result_message_producer.h
    config_message_handler.h
)

add_library(host_interface STATIC
    ${HOST_INTERFACE_SOURCES}
    ${HOST_INTERFACE_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(host_interface
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(host_interface
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        hil_rig_protocol::hil_rig_protocol
        rtos
        hw_timer
        hw_gpio
        hw_usb
        hw_pwm_gen
        exec_digital_output
        exec_analogue_output
        exec_pwm_gen
        execution_manager
        flash_manager
        run_state_manager
)

# Add flash_manager as a private dependency when the documented instruction-
# upload workflow is implemented in host_interface.c.

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HOST_INTERFACE_ENABLE_TESTS "Build tests for host_interface module" ON)

if(HOST_INTERFACE_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(host_process_message_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_process_message.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/host_process_message_test_access.c
    )

    target_link_libraries(host_process_message_tests
        PRIVATE
            project_warnings
            global_config
            cubeide_hal
            hil_rig_protocol::hil_rig_protocol
            rtos
            gtest
            gtest_main
            gmock
    )

    target_include_directories(host_process_message_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/rtos
            ${CMAKE_SOURCE_DIR}/src/run_state_manager
            ${CMAKE_SOURCE_DIR}/src/flash_manager
            ${CMAKE_SOURCE_DIR}/src/execution_manager

            ${CMAKE_SOURCE_DIR}/src/test_configuration

            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_can
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_i2c
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_spi
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_uart

            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_can
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_gpio
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_i2c
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_spi
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_uart
    )

    add_test(NAME host_process_message_tests COMMAND host_process_message_tests)

    add_executable(host_interface_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_interface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/host_interface_test_access.c
        ${CMAKE_CURRENT_SOURCE_DIR}/shared_protocol/tests/c/integration/support/transport_test_endpoint.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/shared_protocol/tests/c/integration/support/transport_test_link.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/shared_protocol/tests/c/integration/support/transport_pair_harness.cpp
    )


    target_link_libraries(host_interface_tests
        PRIVATE
            project_warnings
            global_config
            cubeide_hal
            hil_rig_protocol::hil_rig_protocol
            rtos
            hw_usb
            gtest
            gtest_main
            gmock
    )

    target_include_directories(host_interface_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_CURRENT_SOURCE_DIR}/shared_protocol/tests/c/integration
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/rtos
            ${CMAKE_SOURCE_DIR}/src/run_state_manager
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_can
    )

    add_test(NAME host_interface_tests COMMAND host_interface_tests)

    add_executable(instruction_message_handler_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_instruction_message_handler.cpp
    )

    target_link_libraries(instruction_message_handler_tests
        PRIVATE
            project_warnings
            global_config
            cubeide_hal
            hil_rig_protocol::hil_rig_protocol
            rtos
            execution_manager
            gtest
            gtest_main
            gmock
    )

    target_include_directories(instruction_message_handler_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/rtos
            ${CMAKE_SOURCE_DIR}/src/run_state_manager
            ${CMAKE_SOURCE_DIR}/src/execution_manager
            ${CMAKE_SOURCE_DIR}/src/flash_manager
            ${CMAKE_SOURCE_DIR}/src/test_configuration
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_can
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_spi
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_uart
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_i2c
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_can
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_gpio
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_i2c
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_spi
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_uart
    )

    add_test(NAME instruction_message_handler_tests COMMAND instruction_message_handler_tests)

    add_executable(result_message_producer_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_result_message_producer.cpp
    )

    target_link_libraries(result_message_producer_tests
        PRIVATE
            project_warnings
            global_config
            cubeide_hal
            hil_rig_protocol::hil_rig_protocol
            rtos
            gtest
            gtest_main
            gmock
    )

    target_include_directories(result_message_producer_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/rtos
            ${CMAKE_SOURCE_DIR}/src/flash_manager
    )

    add_test(NAME result_message_producer_tests COMMAND result_message_producer_tests)

    add_executable(config_message_handler_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_config_message_handler.cpp
    )

    target_link_libraries(config_message_handler_tests
        PRIVATE
            project_warnings
            global_config
            cubeide_hal
            hil_rig_protocol::hil_rig_protocol
            rtos
            gtest
            gtest_main
            gmock
    )

    target_include_directories(config_message_handler_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/rtos
            ${CMAKE_SOURCE_DIR}/src/run_state_manager
            ${CMAKE_SOURCE_DIR}/src/test_configuration
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_can
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_spi
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_uart
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_i2c
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_can
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_gpio
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_i2c
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_spi
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_uart
    )

    add_test(NAME config_message_handler_tests COMMAND config_message_handler_tests)

    add_executable(variable_instruction_message_handler_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_variable_instruction_message_handler.cpp
    )

    target_link_libraries(variable_instruction_message_handler_tests
        PRIVATE
            project_warnings
            global_config
            cubeide_hal
            hil_rig_protocol::hil_rig_protocol
            rtos
            execution_manager
            gtest
            gtest_main
            gmock
    )

    target_include_directories(variable_instruction_message_handler_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/rtos
            ${CMAKE_SOURCE_DIR}/src/run_state_manager
            ${CMAKE_SOURCE_DIR}/src/execution_manager
            ${CMAKE_SOURCE_DIR}/src/flash_manager
            ${CMAKE_SOURCE_DIR}/src/test_configuration
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_input
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_can
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_spi
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_uart
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_i2c
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_can
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_gpio
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_i2c
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_capture
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_spi
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_uart
    )

    add_test(NAME variable_instruction_message_handler_tests COMMAND variable_instruction_message_handler_tests)

    add_executable(variable_result_message_producer_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_variable_result_message_producer.cpp
    )

    target_link_libraries(variable_result_message_producer_tests
        PRIVATE
            project_warnings
            global_config
            cubeide_hal
            hil_rig_protocol::hil_rig_protocol
            rtos
            gtest
            gtest_main
            gmock
    )

    target_include_directories(variable_result_message_producer_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/rtos
            ${CMAKE_SOURCE_DIR}/src/flash_manager
    )

    add_test(NAME variable_result_message_producer_tests COMMAND variable_result_message_producer_tests)

endif()
