# src/hardware_low_level/hw_adc/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_ADC_SOURCES
    hw_adc.c
)

set(HW_ADC_HEADERS
    hw_adc.h
)

add_library(hw_adc STATIC
    ${HW_ADC_SOURCES}
    ${HW_ADC_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_adc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_adc
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        hw_timer
        # include any other relevant modules
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HW_ADC_ENABLE_TESTS "Build tests for hw_adc module" ON)

if(HW_ADC_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_adc_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_adc.cpp
    )

    target_link_libraries(hw_adc_tests
        PRIVATE
            project_warnings
            hw_adc
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_adc_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_adc_tests COMMAND hw_adc_tests)

endif()

