# src/test_scheduler/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(TEST_SCHEDULER_SOURCES
    test_scheduler.c
)

set(TEST_SCHEDULER_HEADERS
    test_scheduler.h
)

add_library(test_scheduler STATIC
    ${TEST_SCHEDULER_SOURCES}
    ${TEST_SCHEDULER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(test_scheduler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_scheduler
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

option(TEST_SCHEDULER_ENABLE_TESTS "Build tests for test_scheduler module" ON)

if(TEST_SCHEDULER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(test_scheduler_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_test_scheduler.cpp
    )

    target_link_libraries(test_scheduler_tests
        PRIVATE
            project_warnings
            test_scheduler
            gtest
            gtest_main
            gmock
    )

    target_include_directories(test_scheduler_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME test_scheduler_tests COMMAND test_scheduler_tests)

endif()
