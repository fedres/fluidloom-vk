#pragma once

#include "core/VulkanContext.hpp"
#include "core/MemoryAllocator.hpp"
#include "field/FieldRegistry.hpp"
#include "nanovdb_adapter/GpuGridManager.hpp"

#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>

namespace refinement {

/**
 * @brief Manages adaptive mesh refinement (AMR) for the simulation grid
 *
 * Detects regions needing higher resolution and dynamically rebuilds the
 * NanoVDB grid topology. Supports refinement and coarsening based on field
 * criteria (e.g., vorticity magnitude).
 */
class RefinementManager {
public:
    /**
     * @brief Refinement criteria for topology adaptation
     */
    struct Criteria {
        std::string triggerField = "vorticity";  // Field to monitor
        float refineThreshold = 0.5f;              // Split if value > this
        float coarsenThreshold = 0.1f;             // Merge if value < this
        uint32_t minRefinementLevel = 0;
        uint32_t maxRefinementLevel = 3;
    };

    /**
     * @brief Refinement statistics
     */
    struct Stats {
        uint32_t cellsRefined = 0;
        uint32_t cellsCoarsened = 0;
        uint32_t totalActiveCells = 0;
    };

    /**
     * @brief Initialize refinement manager
     * @param ctx Vulkan context
     * @param allocator GPU memory allocator
     * @param criteria Refinement thresholds and settings
     */
    RefinementManager(core::VulkanContext& ctx,
                      core::MemoryAllocator& allocator,
                      const Criteria& criteria = {});

    /**
     * @brief Cleanup refinement resources
     */
    ~RefinementManager();

    /**
     * @brief Mark cells for refinement based on field criteria
     * @param cmd Command buffer for compute dispatch
     * @param fieldName Name of field to evaluate (e.g., "vorticity")
     */
    void markCells(vk::CommandBuffer cmd, const std::string& fieldName);

    /**
     * @brief Rebuild grid topology based on refinement mask
     * @param gridMgr GPU grid manager
     * @return True if topology changed, false otherwise
     */
    bool rebuildTopology(nanovdb_adapter::GpuGridManager& gridMgr);

    /**
     * @brief Remap all fields to new grid after topology change
     * @param cmd Command buffer for compute dispatch
     * @param oldGrid Original grid resources
     * @param newGrid New grid resources
     * @param fields Field registry to remap
     */
    void remapFields(vk::CommandBuffer cmd,
                    const nanovdb_adapter::GpuGridManager::GridResources& oldGrid,
                    const nanovdb_adapter::GpuGridManager::GridResources& newGrid,
                    field::FieldRegistry& fields);

    /**
     * @brief Get statistics from last refinement operation
     */
    Stats getLastRefinementStats() const { return m_lastStats; }

    /**
     * @brief Update refinement criteria
     */
    void setCriteria(const Criteria& criteria) { m_criteria = criteria; }

    /**
     * @brief Get current refinement criteria
     */
    const Criteria& getCriteria() const { return m_criteria; }

private:
    core::VulkanContext& m_context;
    core::MemoryAllocator& m_allocator;
    Criteria m_criteria;
    Stats m_lastStats;

    // Compute pipelines for refinement operations
    vk::Pipeline m_markPipeline;
    vk::Pipeline m_remapPipeline;
    vk::PipelineLayout m_pipelineLayout;

    // Refinement mask buffer (uint8_t per active voxel)
    core::MemoryAllocator::Buffer m_maskBuffer;

    // Host-side mask staging buffer
    core::MemoryAllocator::Buffer m_maskStagingBuffer;

    // Level tracking (uint8_t per active voxel)
    core::MemoryAllocator::Buffer m_levelBuffer;
    std::vector<uint8_t> m_hostLevels;

    /**
     * @brief Create compute pipelines for mark and remap operations
     */
    void createPipelines();

    /**
     * @brief Destroy compute pipelines
     */
    void destroyPipelines();

    /**
     * @brief Allocate mask buffer for given voxel count
     */
    void allocateMaskBuffer(uint32_t voxelCount);

    /**
     * @brief Allocate and initialize level buffer
     */
    void allocateLevelBuffer(uint32_t voxelCount);

    /**
     * @brief Update levels after topology rebuild
     */
    void updateLevels(const std::vector<nanovdb::Coord>& newLUT,
                      const std::vector<nanovdb::Coord>& oldLUT);
};

} // namespace refinement
