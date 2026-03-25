# src/hardware_low_level/hw_qspi/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_QSPI_SOURCES
    hw_qspi.c
)

set(HW_QSPI_HEADERS
    hw_qspi.h
)

add_library(hw_qspi STATIC
    ${HW_QSPI_SOURCES}
    ${HW_QSPI_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_qspi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_qspi
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

option(HW_QSPI_ENABLE_TESTS "Build tests for QSPI module" ON)

if(HW_QSPI_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_qspi_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_qspi.cpp
    )

    target_link_libraries(hw_qspi_tests
        PRIVATE
            project_warnings
            hw_qspi
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_qspi_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_qspi_tests COMMAND hw_qspi_tests)

endif()

