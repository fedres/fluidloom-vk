# Architecture

This section provides a high-level view of the FluidLoom system architecture, including its components, data structures, and technology stack.

## System Overview

FluidLoom is designed as a modular, data-driven simulation engine. It separates the simulation logic (defined in Lua/GLSL) from the execution engine (C++/Vulkan).

```mermaid
graph TD
    User[User / Lua Script] -->|Configures| Engine[Simulation Engine]
    Engine -->|Compiles| Stencils[Stencil Registry]
    Engine -->|Allocates| Fields[Field Registry]
    Engine -->|Manages| Grid[GPU Grid Manager]
    
    subgraph Execution Loop
        Graph[Graph Executor] -->|Dispatches| Compute[Compute Shaders]
        Compute -->|Reads/Writes| GPU[GPU Memory]
        Graph -->|Syncs| Halo[Halo Manager]
        Halo -->|Exchanges| MPI[MPI / Network]
    end
    
    Engine -->|Drives| Graph
```

## Contents

- **[System Architecture](System_Architecture.md)**: Detailed breakdown of the system layers.
- **[Component Diagrams](Component_Diagrams.md)**: Interaction diagrams for key components.
- **[Data Structures](Database_Schema.md)**: Layout of data in memory (VDB, Fields).
- **[Infrastructure](Infrastructure.md)**: Deployment and hardware requirements.
- **[Tech Stack](Tech_Stack.md)**: Libraries and tools used.
