# src/execution_mid_level/exec_spi/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXEC_SPI_SOURCES
    exec_spi.c
)

set(EXEC_SPI_HEADERS
    exec_spi.h
)

add_library(exec_spi STATIC
    ${EXEC_SPI_SOURCES}
    ${EXEC_SPI_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(exec_spi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exec_spi
    PUBLIC
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXEC_SPI_ENABLE_TESTS "Build tests for exec_spi module" ON)

if(EXEC_SPI_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(exec_spi_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exec_spi.cpp
    )

    target_link_libraries(exec_spi_tests
        PRIVATE
            exec_spi
            gtest
            gtest_main
            gmock
    )

    target_include_directories(exec_spi_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME exec_spi_tests COMMAND exec_spi_tests)

endif()
