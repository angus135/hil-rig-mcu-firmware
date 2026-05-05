# src/hardware_low_level/hw_nand/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_NAND_SOURCES
    hw_nand.c
)

set(HW_NAND_HEADERS
    hw_nand.h
)

add_library(hw_nand STATIC
    ${HW_NAND_SOURCES}
    ${HW_NAND_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_nand
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_nand
    PUBLIC
        global_config
        project_warnings
        hw_qspi
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HW_NAND_ENABLE_TESTS "Build tests for NAND module" ON)

if(HW_NAND_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_nand_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_nand.cpp
    )

    target_link_libraries(hw_nand_tests
        PRIVATE
            project_warnings
            hw_nand
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_nand_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_nand_tests COMMAND hw_nand_tests)

endif()
