# Troubleshooting

Common issues and solutions when developing with FluidLoom.

## Build Issues

### "Could not find Vulkan"
- **Cause**: Vulkan SDK not installed or not in PATH.
- **Solution**: Install Vulkan SDK. On macOS, ensure `VULKAN_SDK` environment variable is set.

### "spdlog linking error"
- **Cause**: Version mismatch between headers and library.
- **Solution**: Ensure you are using the external submodule version. Clean build directory and rebuild.

### "vk_mem_alloc.h not found"
- **Cause**: VMA header missing.
- **Solution**: Install `vulkan-memory-allocator` (via vcpkg or manually) or ensure the submodule is initialized.

## Runtime Issues

### "Failed to initialize volk" / "VK_ERROR_INITIALIZATION_FAILED"
- **Cause**: Vulkan loader cannot find an ICD (driver).
- **Solution**:
  - **macOS**: Set `VK_ICD_FILENAMES` to your MoltenVK JSON (e.g., `/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`).
  - **Linux**: Ensure Mesa/NVIDIA drivers are installed correctly.

### "Failed to create Vulkan instance"
- **Cause**: Missing extensions or incompatible driver.
- **Solution**:
  - **macOS**: Ensure `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` is enabled (handled in code).
  - Check logs for validation layer errors.

### "Shader compilation failed"
- **Cause**: Syntax error in GLSL code or missing `glslc`.
- **Solution**:
  - Check the log output for GLSL errors.
  - Ensure `glslc` is in your PATH.

## Debugging

### Logging
Increase log verbosity to debug issues:
```cpp
// In code
core::Logger::set_level(spdlog::level::debug);
```

### Vulkan Validation Layers
Validation layers are enabled by default in Debug builds. They provide detailed error messages for API misuse. Check the console output for `[Validation]` messages.

### GPU Capture
Use tools like **RenderDoc** or **NVIDIA Nsight** to capture frames and inspect GPU state, buffers, and commands.
