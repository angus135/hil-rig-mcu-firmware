# src/buffer_manager/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(BUFFER_MANAGER_SOURCES
    buffer_manager.c
    instruction_buffer.c
    result_buffer.c
)

set(BUFFER_MANAGER_HEADERS
    buffer_manager.h
    instruction_buffer.h
    result_buffer.h
)

add_library(buffer_manager STATIC
    ${BUFFER_MANAGER_SOURCES}
    ${BUFFER_MANAGER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(buffer_manager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(buffer_manager
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(BUFFER_MANAGER_ENABLE_TESTS "Build tests for buffer_manager module" ON)

if(BUFFER_MANAGER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(buffer_manager_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_buffer_manager.cpp
    )

    target_link_libraries(buffer_manager_tests
        PRIVATE
            project_warnings
            buffer_manager
            gtest
            gtest_main
            gmock
    )

    target_include_directories(buffer_manager_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME buffer_manager_tests COMMAND buffer_manager_tests)

endif()
