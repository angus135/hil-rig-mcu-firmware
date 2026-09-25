# src/run_state_manager/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(RUN_STATE_MANAGER_SOURCES
    run_state_manager.c
    run_metadata.c
    dut_driver_lifecycle.c
)

set(RUN_STATE_MANAGER_HEADERS
    run_state_manager.h
    run_metadata.h
    dut_driver_lifecycle.h
)

add_library(run_state_manager STATIC
    ${RUN_STATE_MANAGER_SOURCES}
    ${RUN_STATE_MANAGER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(run_state_manager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(run_state_manager
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        hw_can
        hw_timer
        hw_gpio
        logic_expander
        flash_manager
        execution_manager
        test_configuration
        host_interface
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(RUN_STATE_MANAGER_ENABLE_TESTS "Build tests for run_state_manager module" ON)

if(RUN_STATE_MANAGER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(run_state_manager_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_run_state_manager.cpp
    )

    target_link_libraries(run_state_manager_tests
        PRIVATE
            project_warnings
            run_state_manager
            gtest
            gtest_main
            gmock
    )

    target_include_directories(run_state_manager_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME run_state_manager_tests COMMAND run_state_manager_tests)

    add_executable(dut_driver_lifecycle_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dut_driver_lifecycle.cpp
    )

    target_link_libraries(dut_driver_lifecycle_tests
        PRIVATE
            project_warnings
            run_state_manager
            gtest
            gtest_main
            gmock
    )

    target_include_directories(dut_driver_lifecycle_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src/test_configuration
            ${CMAKE_SOURCE_DIR}/src/logic_expander
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
            ${CMAKE_SOURCE_DIR}/src/power
    )

    add_test(NAME dut_driver_lifecycle_tests COMMAND dut_driver_lifecycle_tests)

endif()
