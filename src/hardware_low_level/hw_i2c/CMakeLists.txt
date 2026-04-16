# src/hardware_low_level/hw_i2c/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_I2C_SOURCES
    hw_i2c.c
)

set(HW_I2C_HEADERS
    hw_i2c.h
)

add_library(hw_i2c STATIC
    ${HW_I2C_SOURCES}
    ${HW_I2C_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_i2c
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(hw_i2c
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

option(HW_I2C_ENABLE_TESTS "Build tests for HW_I2C module" ON)

if(HW_I2C_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_i2c_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_i2c.cpp
    )

    target_link_libraries(hw_i2c_tests
        PRIVATE
            project_warnings
            hw_i2c
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_i2c_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_i2c_tests COMMAND hw_i2c_tests)

endif()

