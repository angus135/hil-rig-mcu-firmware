# src/run_state_manager/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(RUN_STATE_MANAGER_SOURCES
    run_state_manager.c
    configuration_application.c
)

set(RUN_STATE_MANAGER_HEADERS
    run_state_manager.h
    configuration_application.h
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
        hw_timer
        hw_gpio
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

endif()
