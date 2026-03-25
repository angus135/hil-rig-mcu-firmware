# src/external_flash/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXTERNAL_FLASH_SOURCES
    external_flash.c
)

set(EXTERNAL_FLASH_HEADERS
    external_flash.h
)

add_library(external_flash STATIC
    ${EXTERNAL_FLASH_SOURCES}
    ${EXTERNAL_FLASH_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(external_flash
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(external_flash
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXTERNAL_FLASH_ENABLE_TESTS "Build tests for external_flash module" ON)

if(EXTERNAL_FLASH_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(external_flash_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_external_flash.cpp
    )

    target_link_libraries(external_flash_tests
        PRIVATE
            project_warnings
            external_flash
            gtest
            gtest_main
            gmock
    )

    target_include_directories(external_flash_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME external_flash_tests COMMAND external_flash_tests)

endif()
