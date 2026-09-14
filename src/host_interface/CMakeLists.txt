# src/host_interface/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HOST_INTERFACE_SOURCES
    host_interface.c
    host_process_message.c
    instruction_message_handler.c
)

set(HOST_INTERFACE_HEADERS
    host_interface.h
    host_process_message.h
    instruction_message_handler.h
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
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_digital_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_analogue_output
            ${CMAKE_SOURCE_DIR}/src/execution_mid_level/exec_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_pwm_gen
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_gpio
            ${CMAKE_SOURCE_DIR}/src/execution_manager
            ${CMAKE_SOURCE_DIR}/src/flash_manager
    )

    add_test(NAME instruction_message_handler_tests COMMAND instruction_message_handler_tests)

endif()
