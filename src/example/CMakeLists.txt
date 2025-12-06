# src/example/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXAMPLE_SOURCES
    example.c
)

set(EXAMPLE_HEADERS
    example.h
    example_hal_gpio.h
)

add_library(example STATIC
    ${EXAMPLE_SOURCES}
    ${EXAMPLE_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(example
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(example
    PUBLIC
        cubeide_hal
)

target_link_libraries(console
    PUBLIC
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXAMPLE_ENABLE_TESTS "Build tests for example module" ON)

if(EXAMPLE_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(example_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_example.cpp
    )

    target_link_libraries(example_tests
        PRIVATE
            example
            gtest
            gtest_main
            gmock
    )

    target_include_directories(example_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME example_tests COMMAND example_tests)

endif()
