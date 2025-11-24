# Data Structures

FluidLoom does not use a traditional database. Instead, it manages complex data structures in GPU memory.

## NanoVDB Grid Structure

The core data structure is the **NanoVDB** grid, a read-only (on GPU), compact, sparse voxel data structure.

### Hierarchy
1. **Grid**: Contains metadata (transform, background value) and the root node.
2. **Root Node**: Sparse map to upper internal nodes (Tile nodes).
3. **Internal Nodes**:
   - **Upper (5)**: Maps to Lower nodes.
   - **Lower (4)**: Maps to Leaf nodes.
4. **Leaf Nodes (3)**: Contains an $8 \times 8 \times 8$ block of voxels (values or bitmasks).

### GPU Layout
The entire tree is flattened into a single `vk::Buffer`. Compute shaders traverse this tree to find active voxels.

## Field Buffers

Fields are stored as linear arrays in `vk::Buffer` (Storage Buffers).

### Layout
- **Type**: `float`, `vec3`, etc.
- **Size**: `num_active_voxels * sizeof(Type)`.
- **Indexing**: The NanoVDB grid leaf nodes store an offset (index) into this linear array.
  - `linear_index = leaf_offset + voxel_index_in_leaf`

This structure allows for efficient coalesced memory access and decoupling of topology (VDB) from data (Fields).

## Double Buffering

To support time integration, every field has two buffers:
1. **Current**: Read-only during compute.
2. **Next**: Write-only during compute.

Pointers are swapped at the end of the step.
