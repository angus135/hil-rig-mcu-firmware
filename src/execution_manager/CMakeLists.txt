# src/execution_manager/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXECUTION_MANAGER_SOURCES
    execution_manager.c
)

set(EXECUTION_MANAGER_HEADERS
    execution_manager.h
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
        hw_timer
        hw_gpio
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

endif()
