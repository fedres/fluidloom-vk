# Programming Guide

This guide explains the core concepts, architecture, and design patterns used in FluidLoom. It is intended for developers who want to understand the internal workings of the engine.

## Core Concepts

### Sparse Voxel Grids (NanoVDB)
FluidLoom uses **NanoVDB** for efficient storage and manipulation of sparse volumetric data on the GPU. Unlike dense grids, sparse grids only store data where it exists, allowing for high-resolution simulations in large domains.

### Stencil Operations
The core of the simulation logic is defined using **Stencil Operations**. A stencil operation computes the new value of a voxel based on its neighbors. FluidLoom provides a DSL (Domain Specific Language) embedded in Lua/GLSL to define these operations.

### Domain Decomposition & Halo Exchange
To support large-scale simulations across multiple GPUs, the domain is decomposed into sub-domains. **Halo Exchange** is the process of synchronizing the boundary layers (ghost cells) between neighboring sub-domains to ensure continuity.

### Compute Shaders
All simulation processing happens on the GPU using **Vulkan Compute Shaders**. The engine compiles high-level stencil definitions into optimized GLSL compute shaders at runtime.

## Contents

- **[Key Components](Key_Components.md)**: Detailed breakdown of the engine's modules.
- **[Design Patterns](Design_Patterns.md)**: Architectural patterns used in the codebase.
- **[Data Flow](Data_Flow.md)**: How data moves through the simulation pipeline.
- **[State Management](State_Management.md)**: Handling GPU and CPU state.
- **[Best Practices](Best_Practices.md)**: Guidelines for efficient simulation code.
