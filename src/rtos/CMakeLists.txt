# src/rtos/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(RTOS_CONFIG_SOURCES
    stub_task.c
    stub_queue.c
)

set(RTOS_CONFIG_HEADERS
    rtos_config.h
)

add_library(rtos STATIC
    ${RTOS_CONFIG_SOURCES}
    ${RTOS_CONFIG_HEADERS}
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

# option(RTOS_CONFIG_ENABLE_TESTS "Build tests for rtos module" ON)

# if(RTOS_CONFIG_ENABLE_TESTS AND BUILD_TESTING)

#     add_executable(rtos_config_tests
#         ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rtos_config.cpp
#     )

#     target_link_libraries(rtos_config_tests
#         PRIVATE
#             project_warnings
#             rtos
#             gtest
#             gtest_main
#             gmock
#     )

#     target_include_directories(rtos_config_tests
#         PRIVATE
#             ${CMAKE_CURRENT_SOURCE_DIR}
#             ${CMAKE_CURRENT_SOURCE_DIR}/tests
#     )

#     add_test(NAME rtos_config_tests COMMAND rtos_config_tests)

# endif()
