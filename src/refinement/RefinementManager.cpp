#include "refinement/RefinementManager.hpp"
#include "core/Logger.hpp"

namespace refinement {

RefinementManager::RefinementManager(core::VulkanContext& ctx,
                                     core::MemoryAllocator& allocator,
                                     const Criteria& criteria)
    : m_context(ctx),
      m_allocator(allocator),
      m_criteria(criteria)
{
    LOG_INFO("Initializing RefinementManager");
    LOG_DEBUG("Trigger field: {}", m_criteria.triggerField);
    LOG_DEBUG("Refine threshold: {}", m_criteria.refineThreshold);
    LOG_DEBUG("Coarsen threshold: {}", m_criteria.coarsenThreshold);

    createPipelines();
    LOG_INFO("RefinementManager initialized");
}

RefinementManager::~RefinementManager()
{
    LOG_DEBUG("Destroying RefinementManager");
    destroyPipelines();
}

void RefinementManager::createPipelines()
{
    LOG_DEBUG("Creating refinement compute pipelines");

    // For now, create placeholder pipeline layouts
    // Full shader compilation will be implemented with DXC integration

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 1;

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset = 0;
    pushRange.size = 256; // Max push constant size for refinement parameters

    layoutInfo.pPushConstantRanges = &pushRange;

    auto result = m_context.getDevice().createPipelineLayout(layoutInfo);
    if (result.result != vk::Result::eSuccess) {
        std::string error = "Failed to create pipeline layout: " +
                           vk::to_string(result.result);
        LOG_ERROR(error);
        throw std::runtime_error(error);
    }

    m_pipelineLayout = result.value;
    LOG_DEBUG("Refinement pipeline layout created");

    // Mark and remap pipelines are stubbed - will be compiled with SPIR-V
    // when DXC integration is complete
    m_markPipeline = vk::Pipeline();
    m_remapPipeline = vk::Pipeline();
}

void RefinementManager::destroyPipelines()
{
    LOG_DEBUG("Destroying refinement pipelines");

    if (m_markPipeline) {
        m_context.getDevice().destroyPipeline(m_markPipeline);
    }
    if (m_remapPipeline) {
        m_context.getDevice().destroyPipeline(m_remapPipeline);
    }
    if (m_pipelineLayout) {
        m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
    }
}

void RefinementManager::allocateMaskBuffer(uint32_t voxelCount)
{
    LOG_DEBUG("Allocating mask buffer for {} voxels", voxelCount);

    if (voxelCount == 0) {
        LOG_WARN("Attempting to allocate mask buffer with 0 voxels");
        return;
    }

    // Allocate GPU-side mask buffer
    uint64_t maskSize = static_cast<uint64_t>(voxelCount) * sizeof(uint8_t);

    m_maskBuffer = m_allocator.allocateBuffer(
        maskSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
        vma::MemoryUsage::eGpuOnly,
        "RefinementMask");

    // Allocate host-visible staging buffer for readback
    m_maskStagingBuffer = m_allocator.allocateBuffer(
        maskSize,
        vk::BufferUsageFlagBits::eTransferDst,
        vma::MemoryUsage::eCpuToGpu,
        "RefinementMaskStaging");

    LOG_DEBUG("Mask buffer allocated: {} bytes", maskSize);
}

void RefinementManager::markCells(vk::CommandBuffer cmd,
                                   const std::string& fieldName)
{
    LOG_DEBUG("Marking cells for refinement using field: {}", fieldName);

    if (!m_markPipeline) {
        LOG_WARN("Mark pipeline not yet compiled - skipping refinement trigger");
        return;
    }

    // Bind pipeline
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_markPipeline);

    // Push refinement criteria
    struct PushConstants {
        float refineThreshold;
        float coarsenThreshold;
        uint32_t voxelCount;
        uint32_t _pad;
    } pc{m_criteria.refineThreshold, m_criteria.coarsenThreshold, 0, 0};

    cmd.pushConstants<PushConstants>(m_pipelineLayout,
                                      vk::ShaderStageFlagBits::eCompute,
                                      0,
                                      pc);

    // Dispatch compute shader
    // Grid size will be determined by voxel count (loaded from field)
    // For now, use placeholder dispatch
    uint32_t groupSize = 256;
    uint32_t groupCount = (m_lastStats.totalActiveCells + groupSize - 1) / groupSize;

    if (groupCount > 0) {
        cmd.dispatch(groupCount, 1, 1);
    }

    LOG_DEBUG("Cell marking compute shader dispatched");
}

bool RefinementManager::rebuildTopology(
    nanovdb_adapter::GpuGridManager& gridMgr)
{
    LOG_INFO("Rebuilding grid topology");

    // Download mask from GPU
    auto device = m_context.getDevice();

    auto copyCmd = m_context.beginOneTimeCommand();

    // Copy mask from GPU to staging buffer
    vk::BufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = m_maskBuffer.size;

    copyCmd.copyBuffer(m_maskBuffer.buffer,
                       m_maskStagingBuffer.buffer,
                       region);

    m_context.endOneTimeCommand(copyCmd);

    // Download and analyze mask
    uint8_t* maskData = m_allocator.mapBuffer(m_maskStagingBuffer);
    if (!maskData) {
        LOG_ERROR("Failed to map staging buffer for mask readback");
        return false;
    }

    // Count refinement actions
    uint32_t refined = 0;
    uint32_t coarsened = 0;
    uint32_t total = m_maskBuffer.size; // Size in bytes = voxel count

    for (uint32_t i = 0; i < total; ++i) {
        if (maskData[i] == 1) ++refined;
        else if (maskData[i] == 2) ++coarsened;
    }

    m_allocator.unmapBuffer(m_maskStagingBuffer);

    m_lastStats.cellsRefined = refined;
    m_lastStats.cellsCoarsened = coarsened;
    m_lastStats.totalActiveCells = total;

    LOG_INFO("Refinement analysis: {} refined, {} coarsened, {} total",
             refined, coarsened, total);

    // Topology rebuild would be handled by TopologyRebuilder class
    // For now, return whether any changes were detected
    bool topologyChanged = (refined > 0) || (coarsened > 0);

    if (topologyChanged) {
        LOG_INFO("Grid topology requires update");
    }

    return topologyChanged;
}

void RefinementManager::remapFields(
    vk::CommandBuffer cmd,
    const nanovdb_adapter::GpuGridManager::GridResources& oldGrid,
    const nanovdb_adapter::GpuGridManager::GridResources& newGrid,
    field::FieldRegistry& fields)
{
    LOG_DEBUG("Remapping fields to new grid");

    if (!m_remapPipeline) {
        LOG_WARN("Remap pipeline not yet compiled - skipping field remapping");
        return;
    }

    // Bind pipeline
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_remapPipeline);

    // Push constants for remapping (old/new grid info)
    struct RemapConstants {
        uint32_t oldVoxelCount;
        uint32_t newVoxelCount;
        uint32_t fieldCount;
        uint32_t _pad;
    } pc{oldGrid.voxelCount, newGrid.voxelCount, fields.getFieldCount(), 0};

    cmd.pushConstants<RemapConstants>(m_pipelineLayout,
                                       vk::ShaderStageFlagBits::eCompute,
                                       0,
                                       pc);

    // Dispatch remap shader on new grid size
    uint32_t groupSize = 256;
    uint32_t groupCount = (newGrid.voxelCount + groupSize - 1) / groupSize;

    if (groupCount > 0) {
        cmd.dispatch(groupCount, 1, 1);

        // Memory barrier to ensure field writes complete
        vk::MemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags(),
                            barrier,
                            nullptr,
                            nullptr);
    }

    LOG_DEBUG("Field remapping compute shader dispatched");
}

} // namespace refinement
