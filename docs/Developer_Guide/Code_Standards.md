# Code Standards

To maintain a high-quality codebase, please adhere to the following standards for C++ and Lua.

## C++ Standards

We use **C++20**.

### Naming Conventions

- **Classes/Structs**: `PascalCase` (e.g., `SimulationEngine`, `VulkanContext`)
- **Functions/Methods**: `camelCase` (e.g., `initialize`, `computeStep`)
- **Variables**: `camelCase` (e.g., `voxelCount`, `device`)
- **Member Variables**: `m_camelCase` (e.g., `m_instance`, `m_device`)
- **Constants/Enums**: `PascalCase` or `SCREAMING_SNAKE_CASE` (e.g., `MaxIterations`, `VK_SUCCESS`)
- **Namespaces**: `snake_case` (e.g., `core`, `stencil`, `halo`)

### Formatting

- Use **clang-format** with the project's `.clang-format` file.
- Indentation: 4 spaces.
- Braces: K&R style (opening brace on same line).

### Best Practices

- **RAII**: Use RAII wrappers for all resources (Vulkan handles, memory).
- **Smart Pointers**: Prefer `std::unique_ptr` and `std::shared_ptr` over raw pointers.
- **Const Correctness**: Mark methods and arguments `const` whenever possible.
- **Vulkan-HPP**: Use the C++ Vulkan bindings (`vulkan.hpp`) instead of C API where feasible.
- **Logging**: Use `LOG_INFO`, `LOG_ERROR`, etc. (spdlog wrappers) instead of `std::cout`.

## Lua Standards

- **Variables**: `snake_case` (e.g., `simulation_steps`, `grid_size`)
- **Functions**: `snake_case` (e.g., `create_grid`, `run_simulation`)
- **Indent**: 2 or 4 spaces (be consistent).

## Documentation

- **Doxygen**: Document all public classes and methods using Doxygen style comments.
  ```cpp
  /**
   * @brief Initializes the Vulkan context.
   * @param enableValidation Whether to enable validation layers.
   * @throws std::runtime_error If initialization fails.
   */
  void init(bool enableValidation);
  ```
