# src/hardware_low_level/hw_usb/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(HW_USB_SOURCES
    hw_usb.c
)

set(HW_USB_HEADERS
    hw_usb.h
)

add_library(hw_usb STATIC
    ${HW_USB_SOURCES}
    ${HW_USB_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(hw_usb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hw_usb
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

option(HW_USB_ENABLE_TESTS "Build tests for hw_usb module" ON)

if(HW_USB_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(hw_usb_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hw_usb.cpp
    )

    target_link_libraries(hw_usb_tests
        PRIVATE
            project_warnings
            hw_usb
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(hw_usb_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME hw_usb_tests COMMAND hw_usb_tests)

endif()

