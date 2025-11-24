# Setup and Installation

This guide covers the prerequisites and steps to set up the FluidLoom development environment on macOS, Linux, and Windows.

## Prerequisites

Ensure you have the following tools installed:

- **C++ Compiler**: C++20 compatible (Clang 15+, GCC 11+, MSVC 19.30+).
- **CMake**: Version 3.20 or higher.
- **Vulkan SDK**: Version 1.3 or higher. [Download here](https://vulkan.lunarg.com/).
- **Lua**: Version 5.4.x.
- **OpenSSL**: Required for SHA256 hashing in PipelineCache.
- **Git**: For version control.

### macOS (Homebrew)

```bash
brew install cmake vulkan-loader molten-vk lua openssl
```

### Linux (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install build-essential cmake libvulkan-dev vulkan-tools liblua5.4-dev libssl-dev
```

## Dependencies

FluidLoom uses the following libraries:

| Library | Purpose | Source |
|---------|---------|--------|
| **Vulkan** | Graphics/Compute API | System SDK |
| **vk-bootstrap** | Vulkan initialization | External submodule |
| **volk** | Vulkan meta-loader | System/External |
| **spdlog** | Logging | External submodule |
| **sol2** | Lua/C++ binding | System/External |
| **glm** | Mathematics | System/External |
| **NanoVDB** | Sparse voxel grids | External submodule |

## Building the Project

1. **Clone the repository**:
   ```bash
   git clone --recursive https://github.com/karthikt/fluidloom.git
   cd fluidloom
   ```

2. **Create a build directory**:
   ```bash
   mkdir build
   cd build
   ```

3. **Configure with CMake**:
   ```bash
   cmake ..
   ```
   
   *Optional CMake flags:*
   - `-DCMAKE_BUILD_TYPE=Release`: Build for performance (recommended for simulation).
   - `-DENABLE_TESTS=ON`: Enable unit tests (requires Catch2).

4. **Build**:
   ```bash
   make -j$(nproc)
   ```

## Verifying Installation

Run the minimal integration test to ensure everything is working:

```bash
./fluidloom_app ../tests/integration/minimal_test.lua
```

You should see output indicating successful initialization and simulation steps.
