# Build System Setup

This document outlines the build system requirements, dependencies, and instructions for setting up the Vulkan Fluid Engine.

## Prerequisites

Ensure you have the following tools installed on your system:

- **CMake**: Version 3.28 or higher.
- **C++ Compiler**: C++20 compatible compiler (Clang 16+, GCC 13+, MSVC 19.36+).
- **Vulkan SDK**: Latest stable version (1.3.268.0 or higher recommended).
- **Python**: Version 3.10+ (for script bindings and build tools).
- **Ninja** (Optional but recommended): For faster builds.

## Dependencies

The project relies on the following libraries. These are managed via `vcpkg` or git submodules.

| Library | Version | Purpose |
| :--- | :--- | :--- |
| **vk-bootstrap** | v1.4.328 | Vulkan instance/device creation and validation setup. |
| **volk** | v3.2.0 | Meta-loader for Vulkan API entry points. |
| **VulkanMemoryAllocator** | v3.3.0 | Efficient GPU memory management (VMA). |
| **NanoVDB** | v32.9.0 | GPU-friendly sparse grid data structure. |
| **sol3** | v3.3.0 | C++ bindings for Lua scripting. |
| **DirectXShaderCompiler** | v1.8.2505 | Compiling HLSL/GLSL to SPIR-V at runtime. |
| **glm** | 0.9.9.8 | Mathematics library for graphics software. |
| **spdlog** | 1.12.0 | Fast C++ logging library. |
| **Catch2** | v3.4.0 | Unit testing framework. |

## Build Instructions

### 1. Clone the Repository

```bash
git clone --recursive https://github.com/your-repo/fluid-engine.git
cd fluid-engine
```

### 2. Configure with CMake

We recommend using a build directory separate from the source.

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

**Options:**
- `-DENABLE_TESTS=ON`: Build unit tests (default: ON).
- `-DENABLE_EXAMPLES=ON`: Build example simulations (default: ON).
- `-DENABLE_VALIDATION_LAYERS=ON`: Enable Vulkan validation layers (default: ON for Debug).

### 3. Build

```bash
cmake --build . --config Release --parallel
```

### 4. Run Tests

```bash
ctest --output-on-failure
```

## Project Structure

```
fluid-engine/
├── src/
│   ├── core/           # Core runtime (Vulkan, VMA, Logging)
│   ├── nanovdb_adapter/# NanoVDB integration
│   ├── field/          # Field system (SoA, Registry)
│   ├── domain/         # Domain decomposition
│   ├── halo/           # Halo exchange system
│   ├── stencil/        # DSL compiler and registry
│   ├── graph/          # Execution graph
│   ├── script/         # Lua bindings
│   └── main.cpp        # Entry point
├── external/           # Third-party libraries
├── shaders/            # Compute shaders
├── tests/              # Unit and integration tests
├── scripts/            # User simulation scripts
└── CMakeLists.txt      # Main build configuration
```

## CI/CD Pipeline

The project uses GitHub Actions for CI/CD.

- **Build & Test**: Runs on every push to `main` and PRs.
- **Static Analysis**: Clang-Tidy and CppCheck run on PRs.
- **Documentation**: Doxygen documentation generated on release.
