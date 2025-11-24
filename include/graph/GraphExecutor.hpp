#pragma once

#include "core/VulkanContext.hpp"
#include "halo/HaloManager.hpp"
#include "halo/HaloSync.hpp"
#include "field/FieldRegistry.hpp"
#include "domain/DomainSplitter.hpp"
#include "stencil/StencilRegistry.hpp"

#include <vulkan/vulkan.hpp>
#include <string>
#include <vector>
#include <map>

namespace graph {

/**
 * @brief Executes stencil DAG as Vulkan compute commands
 *
 * Records command buffers for a timestep given:
 * - Execution schedule (topologically sorted stencil list)
 * - Compiled stencil pipelines
 * - Domain decomposition
 * - Halo configuration
 */
class GraphExecutor {
public:
    /**
     * @brief Push constant data for stencil execution
     */
    struct StencilPushConstants {
        uint64_t gridAddr = 0;              // NanoVDB grid device address
        uint64_t bdaTableAddr = 0;          // Field BDA table address
        uint32_t activeVoxelCount = 0;      // Active voxels in this domain
        uint32_t neighborRadius = 0;        // For neighbor access
        float dt = 0.016f;                  // Timestep delta
    };

    /**
     * Initialize graph executor
     * @param context Vulkan context
     * @param haloManager Halo exchange manager
     * @param fieldRegistry Field definitions
     */
    GraphExecutor(const core::VulkanContext& context,
                 halo::HaloManager& haloManager,
                 const field::FieldRegistry& fieldRegistry);

    /**
     * Record command buffer for a single timestep
     * @param cmd Command buffer to record into
     * @param schedule Execution schedule (topologically sorted)
     * @param stencilRegistry Compiled stencils
     * @param domain Domain to execute on
     * @param dt Timestep delta time
     */
    void recordTimestep(vk::CommandBuffer cmd,
                       const std::vector<std::string>& schedule,
                       const stencil::StencilRegistry& stencilRegistry,
                       const domain::SubDomain& domain,
                       float dt = 0.016f);

    /**
     * Record halo exchange operations
     * @param cmd Command buffer
     * @param schedule Stencil schedule
     * @param domain Domain with neighbor information
     */
    void recordHaloExchange(vk::CommandBuffer cmd,
                           const std::vector<std::string>& schedule,
                           const domain::SubDomain& domain);

    const std::vector<vk::Semaphore>& getWaitSemaphores() const { return m_waitSemaphores; }
    const std::vector<vk::Semaphore>& getSignalSemaphores() const { return m_signalSemaphores; }
    const std::vector<uint64_t>& getWaitValues() const { return m_waitValues; }
    const std::vector<uint64_t>& getSignalValues() const { return m_signalValues; }

private:
    const core::VulkanContext& m_context;
    halo::HaloManager& m_haloManager;
    halo::HaloSync m_haloSync;
    const field::FieldRegistry& m_fieldRegistry;

    // Semaphores for the current frame
    std::vector<vk::Semaphore> m_waitSemaphores;
    std::vector<uint64_t> m_waitValues;
    std::vector<vk::Semaphore> m_signalSemaphores;
    std::vector<uint64_t> m_signalValues;

    /**
     * Insert memory barrier between dependent stencils
     * @param cmd Command buffer
     */
    void recordMemoryBarrier(vk::CommandBuffer cmd);

    /**
     * Record a single stencil dispatch
     * @param cmd Command buffer
     * @param stencilName Stencil to dispatch
     * @param stencil Compiled stencil
     * @param pushConstants Push constant data
     * @param domain Domain information
     */
    void recordStencilDispatch(vk::CommandBuffer cmd,
                              const std::string& stencilName,
                              const stencil::CompiledStencil& stencil,
                              const StencilPushConstants& pushConstants,
                              const domain::SubDomain& domain);
};

} // namespace graph
