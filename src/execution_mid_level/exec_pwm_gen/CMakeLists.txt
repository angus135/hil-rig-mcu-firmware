# src/execution_mid_level/exec_pwm_gen/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXEC_PWM_GEN_SOURCES
    exec_pwm_gen.c
)

set(EXEC_PWM_GEN_HEADERS
    exec_pwm_gen.h
)

add_library(exec_pwm_gen STATIC
    ${EXEC_PWM_GEN_SOURCES}
    ${EXEC_PWM_GEN_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(exec_pwm_gen
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exec_pwm_gen
    PUBLIC
        global_config
        rtos
        cubeide_hal
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXEC_PWM_GEN_ENABLE_TESTS "Build tests for exec_pwm_gen module" ON)

if(EXEC_PWM_GEN_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(exec_pwm_gen_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exec_pwm_gen.cpp
    )

    target_link_libraries(exec_pwm_gen_tests
        PRIVATE
            exec_pwm_gen
            gtest
            gtest_main
            gmock
    )

    target_include_directories(exec_pwm_gen_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME exec_pwm_gen_tests COMMAND exec_pwm_gen_tests)

endif()
