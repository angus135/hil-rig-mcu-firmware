# src/logic_expander/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(LOGIC_EXPANDER_SOURCES
    logic_expander.c
)

set(LOGIC_EXPANDER_HEADERS
    logic_expander.h
)

add_library(logic_expander STATIC
    ${LOGIC_EXPANDER_SOURCES}
    ${LOGIC_EXPANDER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(logic_expander
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(logic_expander
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        hw_timer
        hw_gpio
    exec_i2c
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(LOGIC_EXPANDER_ENABLE_TESTS "Build tests for logic_expander module" ON)

if(LOGIC_EXPANDER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(logic_expander_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_logic_expander.cpp
    )

    target_link_libraries(logic_expander_tests
        PRIVATE
            project_warnings
            logic_expander
            gtest
            gtest_main
            gmock
    )

    target_include_directories(logic_expander_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME logic_expander_tests COMMAND logic_expander_tests)

endif()
