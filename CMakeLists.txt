cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

project(HilRigMcuFirmware LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler options / warnings
set(COMMON_WARN_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow              
    -Wundef               
    -Wconversion          
    -Wsign-conversion     
    -Wcast-qual           
    -Wformat=2            
    -Wswitch-enum         
    -Wswitch-default      
    -Wmissing-declarations
    -Wpointer-arith
)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE ${COMMON_WARN_FLAGS})

# Path to CubeIDE project root
set(CUBE_PROJECT_ROOT ${CMAKE_SOURCE_DIR}/f446ze_cubeide_project)

# Interface library that only carries include dirs & compile definitions
add_library(cubeide_hal INTERFACE)

target_include_directories(cubeide_hal
    INTERFACE
        ${CUBE_PROJECT_ROOT}/Core/Inc
        ${CUBE_PROJECT_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc
        ${CUBE_PROJECT_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
        ${CUBE_PROJECT_ROOT}/Drivers/CMSIS/Include
        ${CUBE_PROJECT_ROOT}/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
        ${CUBE_PROJECT_ROOT}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
        ${CUBE_PROJECT_ROOT}/USB_DEVICE/App
        ${CUBE_PROJECT_ROOT}/USB_DEVICE/Target
)

# Match the same defines CubeIDE uses
target_compile_definitions(cubeide_hal
    INTERFACE
        STM32F4xx       
        USE_HAL_DRIVER
)

# Configuration Library
add_library(global_config INTERFACE)

target_include_directories(global_config
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

add_compile_definitions(TEST_BUILD)

# Testing infrastructure
include(CTest)
enable_testing()

include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Add the src tree (which will pull in all modules and their tests)
add_subdirectory(src)
