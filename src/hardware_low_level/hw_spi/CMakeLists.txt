# src/hardware_low_level/hw_spi/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_SPI_SOURCES
    hw_spi.c
)

set(HW_SPI_HEADERS
    hw_spi.h
)

add_library(hw_spi STATIC
    ${HW_SPI_SOURCES}
    ${HW_SPI_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_spi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_spi
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

option(HW_SPI_ENABLE_TESTS "Build tests for HW_SPI module" ON)

if(HW_SPI_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_spi_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_spi.cpp
    )

    target_link_libraries(hw_spi_tests
        PRIVATE
            project_warnings
            hw_spi
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_spi_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_spi_tests COMMAND hw_spi_tests)

endif()

