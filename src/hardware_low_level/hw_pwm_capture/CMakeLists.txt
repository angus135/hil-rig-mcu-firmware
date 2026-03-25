# src/hardware_low_level/hw_pwm_capture/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_PWM_CAPTURE_SOURCES
    hw_pwm_capture.c
)

set(HW_PWM_CAPTURE_HEADERS
    hw_pwm_capture.h
)

add_library(hw_pwm_capture STATIC
    ${HW_PWM_CAPTURE_SOURCES}
    ${HW_PWM_CAPTURE_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_pwm_capture
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_pwm_capture
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        # include any other relevant modules
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HW_PWM_CAPTURE_ENABLE_TESTS "Build tests for PWM capture module" ON)

if(HW_PWM_CAPTURE_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_pwm_capture_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_pwm_capture.cpp
    )

    target_link_libraries(hw_pwm_capture_tests
        PRIVATE
            project_warnings
            hw_pwm_capture
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_pwm_capture_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_pwm_capture_tests COMMAND hw_pwm_capture_tests)

endif()

