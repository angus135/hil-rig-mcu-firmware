# src/app_main/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(APP_MAIN_SOURCES
    app_main.c
)

set(APP_MAIN_HEADERS
    app_main.h
)

add_library(app_main STATIC
    ${APP_MAIN_SOURCES}
    ${APP_MAIN_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(app_main
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(app_main
    PUBLIC
        project_warnings
        cubeide_hal
        rtos
        console
        background
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(APP_MAIN_ENABLE_TESTS "Build tests for app_main module" ON)

if(APP_MAIN_ENABLE_TESTS AND BUILD_TESTING)

    # Use C++ for GoogleTest/Mock
    file(GLOB APP_MAIN_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_main.cpp"
    )

    if(APP_MAIN_TEST_SOURCES)
        add_executable(app_main_tests
            ${APP_MAIN_TEST_SOURCES}
        )

        target_link_libraries(app_main_tests
            PRIVATE
                app_main
                project_warnings
                gtest
                gtest_main
                gmock
        )

        target_include_directories(app_main_tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )

        add_test(NAME app_main_tests COMMAND app_main_tests)
    endif()

endif()
