# src/hardware/hw_gpio/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_GPIO_SOURCES
    hw_gpio.c
)

set(HW_GPIO_HEADERS
    hw_gpio.h
)

add_library(hw_gpio STATIC
    ${HW_GPIO_SOURCES}
    ${HW_GPIO_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_gpio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_gpio
    PUBLIC
        rtos
)


# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

# option(HW_GPIO_ENABLE_TESTS "Build tests for hw_gpio module" ON)

# if(HW_GPIO_ENABLE_TESTS AND BUILD_TESTING)

#     add_executable(hw_gpio_tests
#         ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_gpio.cpp
#     )

#     target_link_libraries(hw_gpio_tests
#         PRIVATE
#             hw_gpio
#             gtest
#             gtest_main
#             gmock
#     )

#     target_include_directories(hw_gpio_tests
#         PRIVATE
#             ${CMAKE_CURRENT_SOURCE_DIR}
#             ${CMAKE_CURRENT_SOURCE_DIR}/tests
#     )

#     add_test(NAME hw_gpio_tests COMMAND hw_gpio_tests)

# endif()
