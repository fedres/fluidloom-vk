# Development Roadmap

This document outlines the modular development plan for the Vulkan Fluid Engine.

## Phase 1: Core Foundation

- [ ] **Module 1: Core Infrastructure**
    - Vulkan instance/device creation (vk-bootstrap, volk).
    - Memory management (VMA).
    - Logging and error handling.
    - Basic compute shader dispatch.

- [ ] **Module 2: NanoVDB Integration**
    - Loading NanoVDB grids from disk.
    - `IndexGrid` generation and management.
    - GPU buffer management for grid topology.

- [ ] **Module 3: Field System**
    - Structure of Arrays (SoA) layout.
    - `FieldRegistry` for dynamic field management.
    - Bindless descriptor setup (BDA).

## Phase 2: Multi-GPU & Domain Decomposition

- [ ] **Module 4: Domain Decomposition**
    - Leaf-node aligned partitioning.
    - `DomainSplitter` implementation.
    - Load balancing strategies.

- [ ] **Module 5: Halo System**
    - Halo buffer allocation (local/remote).
    - Halo exchange logic (peer memory / staging).
    - Synchronization (Timeline Semaphores).

## Phase 3: User API & DSL

- [ ] **Module 6: Stencil DSL & Compiler**
    - DSL parser for user stencils.
    - Runtime shader compilation (DXC).
    - SPIR-V generation and caching.

- [ ] **Module 7: Execution Graph**
    - Dependency analysis (DAG).
    - Command buffer generation.
    - Multi-GPU synchronization.

- [ ] **Module 8: Runtime Scripting**
    - Lua bindings (sol3).
    - Python bindings (pybind11 - optional).
    - User script loading and execution.

## Phase 4: Advanced Features

- [ ] **Module 9: Dynamic Refinement**
    - Refinement criteria and triggers.
    - Topology rebuilding.
    - Field remapping.

- [ ] **Module 10: Visualization**
    - Direct volume rendering via Vulkan.
    - Interop with compute buffers.

## Phase 5: Quality Assurance

- [ ] **Module 11: Testing & Verification**
    - Unit tests (Catch2).
    - Integration tests.
    - Performance benchmarks.

- [ ] **Module 12: Build System & CI/CD**
    - CMake configuration.
    - GitHub Actions workflows.
    - Documentation generation.
