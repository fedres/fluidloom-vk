#include "graph/GraphExecutor.hpp"
#include "core/Logger.hpp"

namespace graph {

GraphExecutor::GraphExecutor(const core::VulkanContext& context,
                            halo::HaloManager& haloManager,
                            const field::FieldRegistry& fieldRegistry)
    : m_context(context),
      m_haloManager(haloManager),
      m_fieldRegistry(fieldRegistry) {
    LOG_INFO("GraphExecutor initialized");
}

void GraphExecutor::recordMemoryBarrier(vk::CommandBuffer cmd) {
    // Barrier: wait for compute writes before reading
    vk::MemoryBarrier barrier{
        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead
    };

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        vk::DependencyFlags{},
        barrier, nullptr, nullptr);
}

void GraphExecutor::recordStencilDispatch(vk::CommandBuffer cmd,
                                         const std::string& stencilName,
                                         const stencil::CompiledStencil& stencil,
                                         const StencilPushConstants& pushConstants,
                                         const domain::SubDomain& domain) {
    LOG_DEBUG("Recording dispatch for stencil: '{}'", stencilName);

    // Bind compute pipeline
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, stencil.pipeline);

    // Push constants
    cmd.pushConstants(stencil.layout,
                     vk::ShaderStageFlagBits::eCompute,
                     0,
                     sizeof(StencilPushConstants),
                     &pushConstants);

    // Calculate thread groups (128 threads per group)
    uint32_t groupCount = (domain.activeVoxelCount + 127) / 128;

    // Dispatch compute shader
    cmd.dispatch(groupCount, 1, 1);

    LOG_DEBUG("  Dispatched {} groups for {} voxels",
              groupCount, domain.activeVoxelCount);
}

void GraphExecutor::recordHaloExchange(vk::CommandBuffer cmd,
                                      const std::vector<std::string>& schedule,
                                      const domain::SubDomain& domain) {
    LOG_DEBUG("Recording halo exchange for domain {}", domain.gpuIndex);

    // This is a placeholder for halo exchange orchestration
    // In full implementation, would:
    // 1. Pack halos from field buffers
    // 2. Exchange with neighbor domains
    // 3. Unpack halos into local buffers
    // 4. Use timeline semaphores for synchronization

    LOG_DEBUG("Halo exchange recorded (full implementation pending)");
}

void GraphExecutor::recordTimestep(vk::CommandBuffer cmd,
                                  const std::vector<std::string>& schedule,
                                  const stencil::StencilRegistry& stencilRegistry,
                                  const domain::SubDomain& domain,
                                  float dt) {
    LOG_INFO("Recording timestep for domain {} ({} stencils, {} voxels)",
             domain.gpuIndex, schedule.size(), domain.activeVoxelCount);

    // Begin command buffer recording
    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };

    try {
        cmd.begin(beginInfo);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to begin command buffer: {}", e.what());
        throw;
    }

    // Record halo exchange if needed
    recordHaloExchange(cmd, schedule, domain);

    // Execute stencils in order
    uint32_t stencilCount = 0;
    for (const auto& stencilName : schedule) {
        try {
            const stencil::CompiledStencil& compiledStencil =
                stencilRegistry.getStencil(stencilName);

            // Prepare push constants
            StencilPushConstants pc{
                .gridAddr = 0,  // Would be filled from domain's GPU grid
                .bdaTableAddr = static_cast<uint64_t>(m_fieldRegistry.getBDATableAddress()),
                .activeVoxelCount = domain.activeVoxelCount,
                .neighborRadius = 1,  // Default for standard stencils
                .dt = dt
            };

            // Record dispatch
            recordStencilDispatch(cmd, stencilName, compiledStencil, pc, domain);

            // Insert barrier between stencils
            if (stencilCount < schedule.size() - 1) {
                recordMemoryBarrier(cmd);
            }

            stencilCount++;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to record stencil '{}': {}", stencilName, e.what());
            cmd.end();
            throw;
        }
    }

    // End command buffer
    try {
        cmd.end();
        LOG_INFO("Timestep command buffer recorded ({} stencils)", stencilCount);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to end command buffer: {}", e.what());
        throw;
    }
}

} // namespace graph
