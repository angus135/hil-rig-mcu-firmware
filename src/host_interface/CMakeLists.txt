# src/host_interface/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HOST_INTERFACE_SOURCES
    host_communications.c
    result_send.c
    test_package_recieve.c
)

set(HOST_INTERFACE_HEADERS
    host_communications.h
    result_send.h
    test_package_recieve.h
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
        rtos
        hw_timer
        hw_gpio
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HOST_INTERFACE_ENABLE_TESTS "Build tests for host_interface module" ON)

if(HOST_INTERFACE_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(host_interface_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_interface.cpp
    )

    target_link_libraries(host_interface_tests
        PRIVATE
            project_warnings
            host_interface
            gtest
            gtest_main
            gmock
    )

    target_include_directories(host_interface_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME host_interface_tests COMMAND host_interface_tests)

endif()
