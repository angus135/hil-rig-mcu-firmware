# src/background/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(BACKGROUND_SOURCES
    background.c
)

set(BACKGROUND_HEADERS
    background.h
)

add_library(background STATIC
    ${BACKGROUND_SOURCES}
    ${BACKGROUND_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(background
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(background
    PUBLIC
        project_warnings
        cubeide_hal
        rtos
        hw_gpio
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(BACKGROUND_ENABLE_TESTS "Build tests for background module" ON)

if(BACKGROUND_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(background_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_background.cpp
    )

    target_link_libraries(background_tests
        PRIVATE
            project_warnings
            background
            gtest
            gtest_main
            gmock
    )

    target_include_directories(background_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME background_tests COMMAND background_tests)

endif()
