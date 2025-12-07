# src/rtos/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(RTOS_SOURCES
    stub_task.c
)

set(RTOS_HEADERS
    rtos.h
)

add_library(rtos STATIC
    ${RTOS_SOURCES}
    ${RTOS_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(rtos
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rtos
    PUBLIC
        project_warnings
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

# option(RTOS_ENABLE_TESTS "Build tests for rtos module" ON)

# if(RTOS_ENABLE_TESTS AND BUILD_TESTING)

#     add_executable(rtos_tests
#         ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rtos.cpp
#     )

#     target_link_libraries(rtos_tests
#         PRIVATE
#             project_warnings
#             rtos
#             gtest
#             gtest_main
#             gmock
#     )

#     target_include_directories(rtos_tests
#         PRIVATE
#             ${CMAKE_CURRENT_SOURCE_DIR}
#             ${CMAKE_CURRENT_SOURCE_DIR}/tests
#     )

#     add_test(NAME rtos_tests COMMAND rtos_tests)

# endif()
