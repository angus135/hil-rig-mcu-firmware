# src/system_monitoring/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(SYSTEM_MONITORING_SOURCES
    system_monitoring.c
    watchdog.c
)

set(SYSTEM_MONITORING_HEADERS
    system_monitoring.h
    watchdog.h
)

add_library(system_monitoring STATIC
    ${SYSTEM_MONITORING_SOURCES}
    ${SYSTEM_MONITORING_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(system_monitoring
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(system_monitoring
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        hw_gpio
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(SYSTEM_MONITORING_ENABLE_TESTS "Build tests for system_monitoring module" ON)

if(SYSTEM_MONITORING_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(system_monitoring_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_system_monitoring.cpp
    )

    target_link_libraries(system_monitoring_tests
        PRIVATE
            project_warnings
            system_monitoring
            gtest
            gtest_main
            gmock
    )

    target_include_directories(system_monitoring_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME system_monitoring_tests COMMAND system_monitoring_tests)

endif()
