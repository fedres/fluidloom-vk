# Building and Deployment

## Build Configuration

FluidLoom uses CMake for build configuration.

### Build Types

- **Debug**: `-DCMAKE_BUILD_TYPE=Debug`
  - Includes debug symbols.
  - No optimizations.
  - Enables assertions.
- **Release**: `-DCMAKE_BUILD_TYPE=Release`
  - Full optimizations (`-O3`).
  - Debug symbols stripped (usually).
  - Recommended for performance testing and production.
- **RelWithDebInfo**: `-DCMAKE_BUILD_TYPE=RelWithDebInfo`
  - Optimized with debug symbols.
  - Good for profiling.

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_TESTS` | `OFF` | Build unit tests. |
| `ENABLE_VALIDATION_LAYERS` | `ON` (Debug) | Enable Vulkan validation layers. |
| `USE_EXTERNAL_NANOVDB` | `OFF` | Use system NanoVDB instead of submodule. |

## Deployment

FluidLoom is currently deployed as a standalone executable.

### Packaging

To create a distribution package:

1. Build in Release mode.
2. Collect artifacts:
   - `fluidloom_app` (executable)
   - `shaders/` (compiled SPIR-V shaders, if pre-compiled)
   - `scripts/` (standard library scripts)
   - `LICENSE`
   - `README.md`

### Environment Variables

The application respects the following environment variables:

- `VK_ICD_FILENAMES`: Path to Vulkan ICD (critical for macOS/MoltenVK).
- `FLUIDLOOM_LOG_LEVEL`: Logging verbosity (`trace`, `debug`, `info`, `warn`, `error`).
- `FLUIDLOOM_SHADER_PATH`: Custom path to shader source files.

### Docker

A `Dockerfile` is available for containerized deployment (WIP).

```bash
docker build -t fluidloom .
docker run --gpus all fluidloom
```
