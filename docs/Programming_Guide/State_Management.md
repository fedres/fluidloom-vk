# State Management

FluidLoom manages state across the CPU and GPU. Correct synchronization is essential.

## GPU-Resident State

The primary simulation state resides in GPU memory to maximize bandwidth.

### Fields
- **Storage**: `vk::Buffer` (Storage Buffers).
- **Layout**: Linear array of values corresponding to active voxels in the NanoVDB grid.
- **Access**: Accessed via `GpuGridManager` using `(i, j, k)` coordinates mapped to linear indices.

### Grid Topology
- **Storage**: `vk::Buffer` (NanoVDB structure).
- **Content**: Tree structure (Root -> Internal Nodes -> Leaf Nodes) defining active regions.
- **Static vs. Dynamic**: Currently static during a time step; dynamic refinement rebuilds this structure.

## CPU-Resident State

Metadata and control logic reside on the CPU.

- **`SimulationEngine`**: Tracks current time, time step index, and global configuration.
- **`FieldRegistry`**: Maps string names ("density") to GPU buffer handles.
- **`DependencyGraph`**: Stores the execution order of nodes.

## Synchronization

### Barriers
We use `vk::PipelineStageFlagBits` and `vk::AccessFlagBits` to define dependencies.

- **Compute-to-Compute**: Ensures a stencil finishes writing before the next stencil reads.
- **Compute-to-Transfer**: Ensures computation finishes before halo exchange.
- **Transfer-to-Compute**: Ensures halo exchange finishes before the next step.

### Fences
Used to synchronize the CPU with the GPU.

- **Frame Fence**: The CPU waits for the previous frame's command buffer to complete before recording a new one (in standard rendering, though for simulation we might just queue up).
- **I/O Fence**: When downloading data for output, the CPU waits for the transfer to complete.
