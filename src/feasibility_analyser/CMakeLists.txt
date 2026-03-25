# src/feasibility_analyser/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(FEASBILITY_ANALYSER_SOURCES
    feasibility_analyser.c
)

set(FEASBILITY_ANALYSER_HEADERS
    feasibility_analyser.h
)

add_library(feasibility_analyser STATIC
    ${FEASBILITY_ANALYSER_SOURCES}
    ${FEASBILITY_ANALYSER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(feasibility_analyser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(feasibility_analyser
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(FEASBILITY_ANALYSER_ENABLE_TESTS "Build tests for feasibility_analyser module" ON)

if(FEASBILITY_ANALYSER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(feasibility_analyser_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_feasibility_analyser.cpp
    )

    target_link_libraries(feasibility_analyser_tests
        PRIVATE
            project_warnings
            feasibility_analyser
            gtest
            gtest_main
            gmock
    )

    target_include_directories(feasibility_analyser_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME feasibility_analyser_tests COMMAND feasibility_analyser_tests)

endif()
