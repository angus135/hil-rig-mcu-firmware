# src/flash_manager/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(FLASH_MANAGER_SOURCES
    flash_manager.c
    instruction_buffer.c
    result_buffer.c
)

set(FLASH_MANAGER_HEADERS
    flash_manager.h
    instruction_buffer.h
    result_buffer.h
)

add_library(flash_manager STATIC
    ${FLASH_MANAGER_SOURCES}
    ${FLASH_MANAGER_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(flash_manager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(flash_manager
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(FLASH_MANAGER_ENABLE_TESTS "Build tests for flash_manager module" ON)

if(FLASH_MANAGER_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(flash_manager_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_flash_manager.cpp
    )

    target_link_libraries(flash_manager_tests
        PRIVATE
            project_warnings
            flash_manager
            gtest
            gtest_main
            gmock
    )

    target_include_directories(flash_manager_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME flash_manager_tests COMMAND flash_manager_tests)

endif()
