# src/host_interface/CMakeLists.txt

# The DEV-138 hardware-test branch consumes only the shared Transport target.
# Explicitly disable every host-only protocol component before adding the submodule.
set(HIL_RIG_PROTOCOL_BUILD_TESTS OFF CACHE BOOL "Build HIL-RIG protocol tests" FORCE)
set(HIL_RIG_PROTOCOL_BUILD_EXAMPLES OFF CACHE BOOL "Build HIL-RIG protocol examples" FORCE)
set(HIL_RIG_PROTOCOL_BUILD_PYTHON OFF CACHE BOOL "Build HIL-RIG protocol Python support" FORCE)

add_subdirectory(shared_protocol EXCLUDE_FROM_ALL)

# The pinned protocol revision currently attaches its still-separate Application
# implementation to the base C target unconditionally. This temporary branch is
# Transport-only, so remove those two sources from the target without changing the
# submodule itself. All Transport/version sources remain exactly as selected there.
get_target_property(HOST_INTERFACE_PROTOCOL_SOURCES hil_rig_protocol SOURCES)
list(FILTER HOST_INTERFACE_PROTOCOL_SOURCES EXCLUDE REGEX "[/\\\\]src[/\\\\]application[/\\\\]")
set_property(TARGET hil_rig_protocol PROPERTY SOURCES "${HOST_INTERFACE_PROTOCOL_SOURCES}")

# -----------------------------
# Library sources / headers
# -----------------------------

set(HOST_INTERFACE_SOURCES
    host_communications.c
    host_transport.c
    protocol_test_harness.c
    result_send.c
    test_package_recieve.c
)

set(HOST_INTERFACE_HEADERS
    host_communications.h
    host_transport.h
    protocol_test_config.h
    protocol_test_harness.h
    result_send.h
    test_package_recieve.h
)

add_library(host_interface STATIC
    ${HOST_INTERFACE_SOURCES}
    ${HOST_INTERFACE_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(host_interface
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(host_interface
    PUBLIC
        global_config
        project_warnings
        cubeide_hal
        rtos
        hw_timer
        hw_gpio
        hw_usb
    PRIVATE
        hil_rig_protocol::hil_rig_protocol
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(HOST_INTERFACE_ENABLE_TESTS "Build tests for host_interface module" ON)

if(HOST_INTERFACE_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(host_interface_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_interface.cpp
    )

    target_link_libraries(host_interface_tests
        PRIVATE
            project_warnings
            host_interface
            gtest
            gtest_main
            gmock
    )

    target_include_directories(host_interface_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME host_interface_tests COMMAND host_interface_tests)

    # Keep the pure codec tests independent of the host_interface target's
    # unrelated hardware dependencies so they can run as focused host tests.
    add_executable(protocol_test_harness_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocol_test_harness.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol_test_harness.c
    )

    target_link_libraries(protocol_test_harness_tests
        PRIVATE
            project_warnings
            gtest
            gtest_main
    )

    target_include_directories(protocol_test_harness_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/shared_protocol/include
    )

    add_test(NAME protocol_test_harness_tests COMMAND protocol_test_harness_tests)

    # Link the real pinned Transport, but compile the integration module directly
    # so USB is provided by the narrow test doubles in test_host_transport.cpp.
    add_executable(host_transport_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_transport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/host_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol_test_harness.c
    )

    target_link_libraries(host_transport_tests
        PRIVATE
            project_warnings
            hil_rig_protocol::hil_rig_protocol
            gtest
            gtest_main
    )

    target_include_directories(host_transport_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/src/hardware_low_level/hw_usb
    )

    add_test(NAME host_transport_tests COMMAND host_transport_tests)

endif()
