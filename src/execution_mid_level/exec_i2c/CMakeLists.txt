# src/execution_mid_level/exec_i2c/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXEC_I2C_SOURCES
    exec_i2c.c
)

set(EXEC_I2C_HEADERS
    exec_i2c.h
)

add_library(exec_i2c STATIC
    ${EXEC_I2C_SOURCES}
    ${EXEC_I2C_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(exec_i2c
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exec_i2c
    PUBLIC
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXEC_I2C_ENABLE_TESTS "Build tests for exec_i2c module" ON)

if(EXEC_I2C_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(exec_i2c_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exec_i2c.cpp
    )

    target_link_libraries(exec_i2c_tests
        PRIVATE
            exec_i2c
            gtest
            gtest_main
            gmock
    )

    target_include_directories(exec_i2c_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME exec_i2c_tests COMMAND exec_i2c_tests)

endif()
