# src/power/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(POWER_SOURCES
    power.c
)

set(POWER_HEADERS
    power.h
)

add_library(power STATIC
    ${POWER_SOURCES}
    ${POWER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(power
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(power
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

option(POWER_ENABLE_TESTS "Build tests for power module" ON)

if(POWER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(power_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_power.cpp
    )

    target_link_libraries(power_tests
        PRIVATE
            project_warnings
            power
            gtest
            gtest_main
            gmock
    )

    target_include_directories(power_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME power_tests COMMAND power_tests)

endif()
