#pragma once

#include "core/MemoryAllocator.hpp"
#include "nanovdb_adapter/GpuGridManager.hpp"

#include <nanovdb/NanoVDB.h>
#include <vector>
#include <cstdint>

namespace refinement {

/**
 * @brief Rebuilds NanoVDB grid topology based on refinement mask
 *
 * Implements hierarchical grid reconstruction after identifying regions
 * that need refinement or coarsening. Maintains NanoVDB's optimized
 * sparse structure.
 */
class TopologyRebuilder {
public:
    /**
     * @brief Initialize topology rebuilder
     * @param allocator GPU memory allocator
     */
    explicit TopologyRebuilder(core::MemoryAllocator& allocator);

    /**
     * @brief Rebuild grid from mask
     *
     * Downloads refinement mask, analyzes topology changes, and constructs
     * new NanoVDB grid with updated leaf structure.
     *
     * @param oldLUT Old lookup table (coordinate list)
     * @param oldValues Old field values
     * @param mask Refinement mask (0=keep, 1=refine, 2=coarsen)
     * @param oldGridRes Old grid resources
     * @return New grid resources (allocator manages memory)
     */
    nanovdb_adapter::GpuGridManager::GridResources
    rebuildGrid(const std::vector<nanovdb::Coord>& oldLUT,
                const std::vector<float>& oldValues,
                const std::vector<uint8_t>& mask,
                const nanovdb_adapter::GpuGridManager::GridResources& oldGridRes);

    /**
     * @brief Validate grid structure
     * @param gridRes Grid resources to validate
     * @return True if grid is valid, false otherwise
     */
    bool validateGrid(const nanovdb_adapter::GpuGridManager::GridResources& gridRes) const;

    /**
     * @brief Get statistics about grid structure
     */
    struct GridStats {
        uint32_t leafCount = 0;
        uint32_t nodeCount = 0;
        uint32_t activeVoxelCount = 0;
        uint64_t memoryUsage = 0;
    };

    /**
     * @brief Compute statistics for grid
     */
    GridStats computeStats(const std::vector<nanovdb::Coord>& lut) const;

private:
    core::MemoryAllocator& m_allocator;

    /**
     * @brief Build new NanoVDB grid from coordinate/value pairs
     */
    std::vector<uint8_t> buildNanoVDBGrid(
        const std::vector<nanovdb::Coord>& coordinates,
        const std::vector<float>& values);

    /**
     * @brief Process refinement actions and compute new coordinates
     */
    std::vector<nanovdb::Coord> processRefinementActions(
        const std::vector<nanovdb::Coord>& oldLUT,
        const std::vector<uint8_t>& mask);

    /**
     * @brief Generate lookup table from grid
     */
    std::vector<nanovdb::Coord> generateLUT(
        const nanovdb::GridHandle<nanovdb::HostBuffer>& handle);

    /**
     * @brief Interpolate value at coordinate in old grid
     */
    float interpolateValue(const std::vector<nanovdb::Coord>& oldLUT,
                          const std::vector<float>& oldValues,
                          const nanovdb::Coord& target) const;
};

} // namespace refinement
