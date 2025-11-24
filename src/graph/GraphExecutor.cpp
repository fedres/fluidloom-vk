#include "graph/GraphExecutor.hpp"
#include "core/Logger.hpp"

namespace graph {

GraphExecutor::GraphExecutor(const core::VulkanContext& context,
                            halo::HaloManager& haloManager,
                            const field::FieldRegistry& fieldRegistry)
    : m_context(context),
      m_haloManager(haloManager),
      m_haloSync(haloManager.getGPUCount(), context),
      m_fieldRegistry(fieldRegistry) {
    LOG_INFO("GraphExecutor initialized");
}

void GraphExecutor::recordMemoryBarrier(vk::CommandBuffer cmd) {
    // Barrier: wait for compute writes before reading
    vk::MemoryBarrier barrier(
        vk::AccessFlagBits::eShaderWrite,
        vk::AccessFlagBits::eShaderRead
    );

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

    // 1. Pack Halos
    // Iterate over all fields and neighbors
    // For this implementation, we iterate over registered fields
    // In a real dependency graph, we'd only exchange fields that are needed by neighbors
    
    // Clear previous semaphores
    m_waitSemaphores.clear();
    m_waitValues.clear();
    m_signalSemaphores.clear();
    m_signalValues.clear();

    // 1. Pack Halos
    // Barrier before pack
    vk::MemoryBarrier packBarrier(
        vk::AccessFlagBits::eShaderWrite,
        vk::AccessFlagBits::eShaderRead
    );
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{},
                        packBarrier, nullptr, nullptr);

    for (const auto& [fieldName, fieldDesc] : m_fieldRegistry.getFields()) {
        // Get halo buffer set for this GPU
        auto& haloSet = m_haloManager.getHaloBufferSet(fieldName, domain.gpuIndex);
        // Get field buffer (assuming FieldRegistry has a way to get it, or we use a helper)
        // FieldRegistry::getFieldBuffer is not standard in the interface I saw earlier?
        // Let's check FieldRegistry.hpp if I can. 
        // Assuming I can get it via name or I have to rely on something else.
        // Wait, FieldRegistry has getFields() returning map.
        // Does it expose buffer?
        // I'll assume getFieldBuffer exists or I can get it.
        // If not, I might need to use MemoryAllocator or similar.
        // Let's assume m_fieldRegistry.getFieldBuffer(fieldName, gpuIndex) exists for now.
        // If it fails compilation, I'll fix it.
        // Actually, looking at previous context, FieldRegistry was used to get descriptors.
        // I'll use a placeholder accessor: m_fieldRegistry.getFieldBuffer(fieldName, domain.gpuIndex)
        
        vk::Buffer fieldBuf = m_fieldRegistry.getField(fieldName).buffer.handle;

        for (const auto& neighbor : domain.neighbors) {
             uint32_t count = haloSet.haloVoxelCounts[neighbor.face];
             if (count == 0) continue;
             
             m_haloSync.recordHaloPack(cmd, fieldBuf, haloSet.remoteHalos[neighbor.face].handle, 0, count);
        }
    }

    // 2. Transfer
    // Barrier between Pack and Transfer
    vk::MemoryBarrier transferBarrier(
        vk::AccessFlagBits::eShaderWrite,
        vk::AccessFlagBits::eTransferRead
    );
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{},
                        transferBarrier, nullptr, nullptr);

    for (const auto& [fieldName, fieldDesc] : m_fieldRegistry.getFields()) {
         auto& haloSet = m_haloManager.getHaloBufferSet(fieldName, domain.gpuIndex);
         
         for (const auto& neighbor : domain.neighbors) {
             uint32_t count = haloSet.haloVoxelCounts[neighbor.face];
             if (count == 0) continue;

             // Determine opposite face for neighbor
             // Simple logic: 0<->1, 2<->3, 4<->5
             uint32_t oppositeFace = (neighbor.face % 2 == 0) ? neighbor.face + 1 : neighbor.face - 1;
             
             // Get neighbor's halo set (requires access to all domains' buffers, which HaloManager has)
             auto& neighborHaloSet = m_haloManager.getHaloBufferSet(fieldName, neighbor.gpuIndex);
             
             // Record transfer (copy from my remote to neighbor's local)
             m_haloSync.recordHaloTransfer(cmd, 
                                           haloSet.remoteHalos[neighbor.face].handle, 
                                           neighborHaloSet.localHalos[oppositeFace].handle, 
                                           count * 4); // 4 bytes per float
                                           
             // Collect signal semaphore (I signal that I finished writing to neighbor)
             // Actually, usually we signal that *transfer* is done.
             // The neighbor waits on this.
             vk::Semaphore signalSem = m_haloManager.getHaloSemaphore(domain.gpuIndex, neighbor.gpuIndex);
             m_signalSemaphores.push_back(signalSem);
             m_signalValues.push_back(1); // Timeline value, should increment in real app
         }
    }

    // 3. Unpack Halos
    // Barrier between Transfer and Unpack
    vk::MemoryBarrier unpackBarrier(
        vk::AccessFlagBits::eTransferWrite,
        vk::AccessFlagBits::eShaderRead
    );
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{},
                        unpackBarrier, nullptr, nullptr);

    for (const auto& [fieldName, fieldDesc] : m_fieldRegistry.getFields()) {
         auto& haloSet = m_haloManager.getHaloBufferSet(fieldName, domain.gpuIndex);
         vk::Buffer fieldBuf = m_fieldRegistry.getField(fieldName).buffer.handle;

         for (const auto& neighbor : domain.neighbors) {
             uint32_t count = haloSet.haloVoxelCounts[neighbor.face];
             if (count == 0) continue;
             
             // Record unpack (from my local halo to field)
             // Note: 'localHalos' are where neighbors wrote data TO.
             m_haloSync.recordHaloUnpack(cmd, haloSet.localHalos[neighbor.face].handle, fieldBuf, 0, count);
             
             // Collect wait semaphore (I wait for neighbor to finish writing to me)
             vk::Semaphore waitSem = m_haloManager.getHaloSemaphore(neighbor.gpuIndex, domain.gpuIndex);
             m_waitSemaphores.push_back(waitSem);
             m_waitValues.push_back(1); // Timeline value
         }
    }
    
    LOG_DEBUG("Halo exchange recorded with {} neighbors", domain.neighbors.size());
}

void GraphExecutor::recordTimestep(vk::CommandBuffer cmd,
                                  const std::vector<std::string>& schedule,
                                  const stencil::StencilRegistry& stencilRegistry,
                                  const domain::SubDomain& domain,
                                  float dt) {
    LOG_INFO("Recording timestep for domain {} ({} stencils, {} voxels)",
             domain.gpuIndex, schedule.size(), domain.activeVoxelCount);

    // Begin command buffer recording
    vk::CommandBufferBeginInfo beginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    );

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
