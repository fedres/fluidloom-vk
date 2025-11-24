#pragma once

#include "core/MemoryAllocator.hpp"
#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/GridBuilder.h>
#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace core {
class VulkanContext;
class MemoryAllocator;
} // namespace core

namespace nanovdb_adapter {

/**
 * @brief GPU-side NanoVDB grid structure (shader interop)
 *
 * Matches std430 memory layout for GLSL push constants
 */
struct PNanoVDB {
    uint64_t rawGridAddress;      // Device address of full NanoVDB grid
    uint64_t lutCoordsAddress;    // Device address of coordinate lookup table
    uint64_t linearValuesAddress; // Device address of linear values
    uint32_t activeVoxelCount;    // Number of active voxels
    uint32_t _pad;                // Padding for 8-byte alignment
};

/**
 * @brief Manages GPU-resident NanoVDB grids
 *
 * Uploads NanoVDB grids to GPU memory and provides shader-compatible structures.
 */
class GpuGridManager {
public:
    /**
     * @brief GPU grid resources descriptor
     */
    struct GridResources {
        core::MemoryAllocator::Buffer rawGrid;      // Full NanoVDB grid structure
        core::MemoryAllocator::Buffer lutCoords;    // Sorted coordinates for reverse lookup
        core::MemoryAllocator::Buffer linearValues; // Values in Morton order
        uint32_t activeVoxelCount;
        nanovdb::CoordBBox bounds;

        /**
         * Get shader-compatible structure
         */
        PNanoVDB getShaderStruct() const {
            return PNanoVDB{
                .rawGridAddress = static_cast<uint64_t>(rawGrid.deviceAddress),
                .lutCoordsAddress = static_cast<uint64_t>(lutCoords.deviceAddress),
                .linearValuesAddress = static_cast<uint64_t>(linearValues.deviceAddress),
                .activeVoxelCount = activeVoxelCount,
                ._pad = 0
            };
        }
    };

    /**
     * Initialize GPU grid manager
     * @param context Vulkan context
     * @param allocator Memory allocator for buffer allocation
     */
    GpuGridManager(const core::VulkanContext& context,
                  core::MemoryAllocator& allocator);

    /**
     * Upload grid from host to GPU
     * @param grid Host-resident NanoVDB grid
     * @return GPU resources descriptor
     */
    GridResources upload(const nanovdb::GridHandle<nanovdb::HostBuffer>& grid);

    /**
     * Cleanup and deallocate GPU grid resources
     * @param resources Resources to destroy
     */
    void destroyGrid(GridResources& resources);

private:
    const core::VulkanContext& m_context;
    core::MemoryAllocator& m_allocator;

    // Helper to compute Morton code (Z-order curve)
    static uint64_t getMortonCode(uint32_t x, uint32_t y, uint32_t z);
};

} // namespace nanovdb_adapter
