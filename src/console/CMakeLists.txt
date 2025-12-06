# src/console/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(CONSOLE_SOURCES
    console.c
)

set(CONSOLE_HEADERS
    console.h
)

add_library(console STATIC
    ${CONSOLE_SOURCES}
    ${CONSOLE_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(console
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(console
    PUBLIC
        cubeide_hal
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(CONSOLE_ENABLE_TESTS "Build tests for console module" ON)

if(CONSOLE_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(console_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_console.cpp
    )

    target_link_libraries(console_tests
        PRIVATE
            console
            gtest
            gtest_main
            gmock
    )

    target_include_directories(console_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME console_tests COMMAND console_tests)

endif()
