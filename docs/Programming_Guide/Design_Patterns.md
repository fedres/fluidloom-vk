# Design Patterns

FluidLoom employs several standard design patterns to ensure modularity, scalability, and performance.

## Singleton Pattern

Used for global services that need to be accessible throughout the application.

- **`core::Logger`**: Provides a centralized logging interface (spdlog wrapper).
- **`core::VulkanContext`**: (Effectively a singleton within the engine scope) Manages the single Vulkan instance.

## Factory / Registry Pattern

Used to manage dynamic objects and resources.

- **`StencilRegistry`**: Creates and stores compute pipelines based on string identifiers.
- **`FieldRegistry`**: Manages field buffers and metadata, allowing lookup by name.

## Command Pattern

The simulation logic is structured as a graph of executable nodes.

- **`IGraphNode`**: Abstract base class for all execution nodes (`ComputeNode`, `HaloNode`, etc.).
- **`GraphExecutor`**: Invokes the `execute` method on nodes in topological order.

## RAII (Resource Acquisition Is Initialization)

Strictly followed for resource management, especially Vulkan objects.

- **`vk::UniqueHandle`**: Used via `vulkan.hpp` to ensure Vulkan objects (Buffers, Images, Semaphores) are destroyed when they go out of scope.
- **`MemoryAllocator`**: Wraps VMA allocations to ensure memory is freed.

## Double Buffering

Used for time integration to allow reading the previous state while writing the new state.

- **Fields**: Each field has a `current` and `next` buffer. At the end of a time step, the pointers are swapped.

## Component-Based Architecture

The engine is composed of loosely coupled components (`RefinementManager`, `HaloManager`, `VisManager`) that can be enabled or disabled independently.
