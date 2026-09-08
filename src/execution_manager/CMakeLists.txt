# src/execution_manager/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXECUTION_MANAGER_SOURCES
    execution_manager.c
    execution_operation_adapters.c
)

set(EXECUTION_MANAGER_HEADERS
    execution_instruction.h
    execution_manager.h
    execution_manager_isr.h
    execution_operation_payloads.h
    execution_operation_adapters.h
)

add_library(execution_manager STATIC
    ${EXECUTION_MANAGER_SOURCES}
    ${EXECUTION_MANAGER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(execution_manager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(execution_manager
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        flash_manager
        exec_digital_output
        exec_pwm_gen
        exec_uart
        exec_spi
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXECUTION_MANAGER_ENABLE_TESTS "Build tests for execution_manager module" ON)

if(EXECUTION_MANAGER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(execution_manager_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_execution_manager.cpp
    )

    target_link_libraries(execution_manager_tests
        PRIVATE
            project_warnings
            execution_manager
            gtest
            gtest_main
            gmock
    )

    target_include_directories(execution_manager_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME execution_manager_tests COMMAND execution_manager_tests)

    add_executable(execution_operation_adapters_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_execution_operation_adapters.cpp
    )

    target_link_libraries(execution_operation_adapters_tests
        PRIVATE
            project_warnings
            execution_manager
            gtest
            gtest_main
    )

    target_include_directories(execution_operation_adapters_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(NAME execution_operation_adapters_tests COMMAND execution_operation_adapters_tests)

endif()
