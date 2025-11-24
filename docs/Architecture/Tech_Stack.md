# Tech Stack

## Core Technologies

| Technology | Version | Purpose | Why Chosen? |
|------------|---------|---------|-------------|
| **C++** | 20 | Core Engine | Performance, control, and modern features (concepts, modules). |
| **Vulkan** | 1.3 | Graphics/Compute | Explicit control over GPU, cross-platform, high performance compute. |
| **Lua** | 5.4 | Scripting | Lightweight, fast, easy to embed, familiar to game devs. |
| **NanoVDB** | Latest | Sparse Grids | Industry standard for sparse volumes, GPU-friendly. |
| **CMake** | 3.20+ | Build System | Industry standard, cross-platform. |

## Libraries

- **vk-bootstrap**: Simplifies Vulkan instance/device creation.
- **volk**: Meta-loader for Vulkan function pointers.
- **spdlog**: Fast, header-only C++ logging library.
- **sol2**: C++ binding library for Lua. High performance and clean syntax.
- **glm**: OpenGL Mathematics. Header-only, GLSL-compatible math types.
- **Catch2**: Unit testing framework. Modern, header-only.

## Tools

- **Clang/GCC**: Compilers.
- **RenderDoc**: GPU debugging and profiling.
- **GitHub Actions**: CI/CD.
