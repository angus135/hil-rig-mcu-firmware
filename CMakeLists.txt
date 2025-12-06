cmake_minimum_required(VERSION 3.15)

project(HilRigMcuFirmware LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler options / warnings
set(COMMON_WARN_FLAGS
    -Wall
    -Wextra
    -Wpedantic
)
add_compile_options(${COMMON_WARN_FLAGS})

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
