# src/hardware_low_level/hw_watchdog/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_WATCHDOG_SOURCES
    hw_watchdog.c
)

set(HW_WATCHDOG_HEADERS
    hw_watchdog.h
)

add_library(hw_watchdog STATIC
    ${HW_WATCHDOG_SOURCES}
    ${HW_WATCHDOG_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_watchdog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_watchdog
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        # include any other relevant modules
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HW_WATCHDOG_ENABLE_TESTS "Build tests for watchdog module" ON)

if(HW_WATCHDOG_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_watchdog_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_watchdog.cpp
    )

    target_link_libraries(hw_watchdog_tests
        PRIVATE
            project_warnings
            hw_watchdog
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_watchdog_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_watchdog_tests COMMAND hw_watchdog_tests)

endif()

