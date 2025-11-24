# Key Components

FluidLoom is built from several modular components that interact to perform the simulation.

## Core Components

### `SimulationEngine`
The central orchestrator of the application. It initializes the system, manages the simulation loop, and exposes the API to Lua scripts.
- **Location**: `src/script/SimulationEngine.cpp`
- **Responsibilities**: Initialization, Time stepping, Script binding.

### `VulkanContext`
Manages the Vulkan instance, physical and logical devices, queues, and memory allocator (VMA).
- **Location**: `src/core/VulkanContext.cpp`
- **Responsibilities**: Device selection, Resource creation, Command pool management.

### `GpuGridManager`
Handles the lifecycle of NanoVDB grids on the GPU. It manages the upload of grid topology and data.
- **Location**: `src/nanovdb_adapter/GpuGridManager.cpp`
- **Responsibilities**: Grid creation, Buffer management, NanoVDB integration.

## Field & Stencil Management

### `FieldRegistry`
Manages the simulation fields (e.g., density, velocity). It allocates GPU buffers for each field and handles double-buffering for time integration.
- **Location**: `src/field/FieldRegistry.cpp`
- **Responsibilities**: Field allocation, Buffer swapping, Texture views.

### `StencilRegistry`
Compiles and manages stencil operations. It takes GLSL source code, generates compute pipelines, and caches them.
- **Location**: `src/stencil/StencilRegistry.cpp`
- **Responsibilities**: Shader compilation, Pipeline creation, Descriptor set management.

## Execution & Synchronization

### `GraphExecutor`
Executes the dependency graph of simulation nodes. It records command buffers for compute dispatches and synchronization barriers.
- **Location**: `src/graph/GraphExecutor.cpp`
- **Responsibilities**: Dependency resolution, Command recording, Fence synchronization.

### `HaloManager`
Manages the exchange of data between sub-domains. It handles the extraction of boundary data and updates ghost cells.
- **Location**: `src/halo/HaloManager.cpp`
- **Responsibilities**: Neighbor detection, Data packing/unpacking, MPI/Vulkan synchronization.
