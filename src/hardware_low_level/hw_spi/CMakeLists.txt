# src/hardware_low_level/hw_spi/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_SPI_SOURCES
    hw_spi_config.c
    hw_spi_rx.c
    hw_spi_tx_config.c
    hw_spi_tx_master.c
    hw_spi_tx_slave.c
)

set(HW_SPI_HEADERS
    hw_spi.h
    hw_spi_internal.h
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
        hw_timer
        hw_gpio
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HW_SPI_ENABLE_TESTS "Build tests for HW_SPI module" ON)

if(HW_SPI_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_spi_rx_tests
        tests/test_hw_spi_rx.cpp
    )

    target_link_libraries(hw_spi_rx_tests
        PRIVATE
            project_warnings
            hw_spi
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_spi_rx_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_spi_rx_tests COMMAND hw_spi_rx_tests)


    add_executable(hw_spi_tx_slave_tests
        tests/test_hw_spi_tx_slave.cpp
    )

    target_link_libraries(hw_spi_tx_slave_tests
        PRIVATE
            project_warnings
            hw_spi
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_spi_tx_slave_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_spi_tx_slave_tests COMMAND hw_spi_tx_slave_tests)


    add_executable(hw_spi_tx_master_tests
        tests/test_hw_spi_tx_master.cpp
    )

    target_link_libraries(hw_spi_tx_master_tests
        PRIVATE
            project_warnings
            hw_spi
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_spi_tx_master_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_spi_tx_master_tests COMMAND hw_spi_tx_master_tests)

endif()