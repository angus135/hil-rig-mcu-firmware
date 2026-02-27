# HIL-RIG-MCU-FIRMWARE  
Firmware Architecture, Build System, and Development Environment

## Overview

`hil-rig-mcu-firmware` is the firmware layer for a Hardware-in-the-Loop (HIL) test rig designed to validate embedded controllers.  
The rig enables deterministic hardware simulation, automated firmware deployment, controlled I/O signalling, and repeatable test execution. It is intended to support development workflows where embedded hardware behaviour must be verified early and continuously.

This repository provides:

- a portable, module-based embedded application framework in C
- a deterministic unit testing environment using **Docker**, **CMake**, **GoogleTest**, and **GoogleMock**
- strict naming and formatting enforcement using `.clang-format` and `.clang-tidy`
- The use of **STM32CubeIDE**-generated code without modifying CubeMX output

**CubeIDE handles MCU startup, HAL configuration, and clock/peripheral initialisation.**  
**This repository provides the application modules that run on top of that environment.**

---

## Repository Structure

```text
hil-rig-mcu-firmware/
├── f446ze_cubeide_project/    # Cube IDE Project with associated generated drivers
├── src/                       # Core modules and unit-testable application logic
│   ├── app_main/              # Entry point invoked from CubeIDE firmware
│   ├── example/               # Example module demonstrating all patterns
│   └── ...                    # Additional modules here
├── .devcontainer/             # VS Code remote container config
├── docker-compose.yml         # Defines dev and CI containers
├── Dockerfile                 # Base toolchain image for builds and tests
├── .clang-format              # C/C++ formatting rules
├── .clang-tidy                # Lint rules and naming conventions
└── README.md                  # This file
```

---

## Development Workflow

The entire repository is designed so that **no host toolchain** is required.  
All development, formatting, builds, and testing occur inside Docker.

---

## 1. Environment Setup

### Requirements

| Tool | Purpose |
|------|--------|
| Docker Desktop | Runs the dev environment |
| VS Code | Main development environment |
| Remote - Containers extension | Enables VS Code to open the repo inside Docker |
| STM32CubeIDE | Builds, flashes, and debugs MCU firmware |


---

## 2. Opening the Project in VS Code

1. Open the repository folder in VS Code
2. You should be prompted to: `Reopen in Container`. If not:
```
Ctrl+Shift+P → Dev Containers: Rebuild and Reopen in Container
```
Once inside the remote container, the following tools are available:

- gcc / g++  
- CMake  
- clang-format (auto-runs on save)  
- clang-tidy  
- GoogleTest / GoogleMock  

No additional installation is required.

---

## 3. Building and Running Tests

All unit tests are built using CMake, completely independent of CubeIDE.

### Configure and Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

### Note: To be able to get correct syntax highlighting you must run a cmake configuration first.

### Run All Tests

```bash
ctest --output-on-failure
```

### Run a Single Module Test

Each module produces its own test binary:

```bash
./src/example/example_tests
```

Expected output:
```
[==========] Running X tests from Y test suites.
[ PASSED ] All tests passed.
```

---

## 4. STM32CubeIDE Firmware Build

Only hardware-specific firmware is built in CubeIDE.  
Application code lives in `src/` and is invoked from CubeIDE’s `main.c` via:

```c
APP_MAIN_Application();
```

### Steps
1. Open this directory in CubeIDE (i.e `hil-rig-mcu-firmware/`)
2. Go File -> Import... -> General -> Existing Projects into Workspace
3. Browse and make sure you are in this directory (`hil-rig-mcu-firmware/`) and click Select Folder
4. Click Finish.
5. Build and flash using CubeIDE normally.

**CubeIDE-generated files must not be edited manually.**

TODO: Add hardware flashing process.

---

## 5. Testing procedure and approach

Two complementary testing techniques are used, depending on the level being
tested.


### 1. TEST_BUILD stubs (eg: low-level hardware - hw_xxx)

Low-level modules such as hw_gpio and hw_uart use #ifdef TEST_BUILD inside
their .c files to stub out hardware access. This guarantees the code compiles
and links on the host without requiring hardware or HAL support.

Example:
```c
void HW_GPIO_Toggle(GPIO_T gpio)
{
#ifdef TEST_BUILD
    (void)gpio;
#else
    switch (gpio)
    {
        case GPIO_GREEN_LED_INDICATOR:
            HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
            break;
        default:
            break;
    }
#endif
}
```

This pattern is intended for simple, direct hardware actions with minimal
internal logic.


### 2. Google Mock via link seam (higher-level logic)

For higher-level application logic (console commands, protocols, state
machines), Google Mock is used to verify behaviour. Production code remains
pure C; all C++ and mocking lives in test_*.cpp files.

Tests replace hw_* functions using a link seam by providing test-only
implementations with the same C symbols.

Example test-only replacement:

```cpp
extern "C" bool HW_UART_Tx(HW_UART_T uart,
                            const uint8_t* data,
                            uint16_t len)
{
    return g_mock->Tx(uart, data, len);
}
```

Example gMock expectation:
```cpp
EXPECT_CALL(mock, Tx(HW_UART_CONSOLE, _, _)).Times(1);
```

This allows tests to assert intent (e.g. "UART transmit requested") without
linking the real hardware implementation.


### When to use each approach
- Use TEST_BUILD stubs for simple, low-level hardware wrappers.
- Use Google Mock when testing complex logic that depends on hardware.

Both approaches are expected to coexist within the modules layer.
