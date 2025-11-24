# Best Practices

Guidelines for writing efficient and maintainable code in FluidLoom.

## Performance

### Minimize CPU-GPU Transfers
Data transfer over PCIe is slow. Keep data on the GPU as long as possible. Only download data for visualization or file output.

### Batch Compute Dispatches
Vulkan command recording has overhead. Record as many commands as possible into a single command buffer and submit them in batches.

### Optimize Stencil Shaders
- **Shared Memory**: Use thread group shared memory for stencils with large footprints (e.g., convolution).
- **Coalesced Access**: Access memory in a pattern that allows the GPU to coalesce transactions.
- **Branching**: Avoid divergent branching within a warp/wavefront.

## Code Quality

### Use the Registry
Do not manually manage buffers for fields. Always use `FieldRegistry` to ensure proper lifecycle management and double-buffering.

### Error Handling
- Check `VkResult` for all Vulkan calls.
- Use `LOG_CHECK` macros to assert conditions and throw exceptions with context.

### Modularity
- Keep components decoupled.
- Use interfaces (`IGraphNode`) to define interactions.
- Avoid global state where possible (use dependency injection or the `SimulationEngine` context).
