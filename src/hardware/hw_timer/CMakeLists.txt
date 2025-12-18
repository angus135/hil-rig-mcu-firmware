# src/hardware/hw_timer/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_TIMER_SOURCES
    hw_timer.c
)

set(HW_TIMER_HEADERS
    hw_timer.h
)

add_library(hw_timer STATIC
    ${HW_TIMER_SOURCES}
    ${HW_TIMER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_timer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_timer
    PUBLIC
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

# option(HW_TIMER_ENABLE_TESTS "Build tests for hw_timer module" ON)

# if(HW_TIMER_ENABLE_TESTS AND BUILD_TESTING)

#     add_executable(hw_timer_tests
#         ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_timer.cpp
#     )

#     target_link_libraries(hw_timer_tests
#         PRIVATE
#             hw_timer
#             gtest
#             gtest_main
#             gmock
#     )

#     target_include_directories(hw_timer_tests
#         PRIVATE
#             ${CMAKE_CURRENT_SOURCE_DIR}
#             ${CMAKE_CURRENT_SOURCE_DIR}/tests
#     )

#     add_test(NAME hw_timer_tests COMMAND hw_timer_tests)

# endif()
