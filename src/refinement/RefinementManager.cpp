#include "refinement/RefinementManager.hpp"
#include "core/Logger.hpp"

#include <fstream>
#include <cstdlib>
#include <cstdio>

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

void refinement::RefinementManager::createPipelines()
{
    LOG_DEBUG("Creating refinement compute pipelines");

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 1;

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset = 0;
    pushRange.size = 256;

    layoutInfo.pPushConstantRanges = &pushRange;

    auto result = m_context.getDevice().createPipelineLayout(layoutInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    m_pipelineLayout = result.value;

    // --- Compile Mark Shader ---
    std::string markShaderSource = R"(
#version 460
layout(local_size_x = 256) in;
layout(push_constant) uniform PC {
    float refineThreshold;
    float coarsenThreshold;
    uint voxelCount;
    uint maxLevel;
} pc;

layout(std430, binding = 0) buffer MaskBuffer {
    uint8_t mask[];
};

layout(std430, binding = 1) buffer FieldBuffer {
    float fieldData[];
};

layout(std430, binding = 2) buffer LevelBuffer {
    uint8_t levels[];
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.voxelCount) return;

    float val = abs(fieldData[idx]);
    uint8_t currentLevel = levels[idx];
    uint8_t action = 0; // 0: None

    if (val > pc.refineThreshold && currentLevel < uint8_t(pc.maxLevel)) {
        action = 1; // Refine (only if below max level)
    } else if (val < pc.coarsenThreshold && currentLevel > 0) {
        action = 2; // Coarsen (only if above base level)
    }

    mask[idx] = action;
}
)";
    // Compile logic (duplicated for now, should be shared)
    std::string uuid = "mark_" + std::to_string(std::rand());
    std::string glslFile = "/tmp/" + uuid + ".comp";
    std::string spvFile = "/tmp/" + uuid + ".spv";
    {
        std::ofstream out(glslFile);
        out << markShaderSource;
    }
    std::system(("/opt/homebrew/bin/glslc -fshader-stage=compute -o " + spvFile + " " + glslFile).c_str());
    std::remove(glslFile.c_str());
    
    std::vector<uint32_t> markSpirv;
    {
        std::ifstream in(spvFile, std::ios::binary | std::ios::ate);
        if (in) {
            size_t size = in.tellg();
            markSpirv.resize(size/4);
            in.seekg(0);
            in.read((char*)markSpirv.data(), size);
        }
    }
    std::remove(spvFile.c_str());

    vk::ShaderModuleCreateInfo markModuleInfo{
        .codeSize = markSpirv.size() * 4,
        .pCode = markSpirv.data()
    };
    vk::ShaderModule markModule = m_context.getDevice().createShaderModule(markModuleInfo);

    vk::ComputePipelineCreateInfo markPipelineInfo{
        .stage = {
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = markModule,
            .pName = "main"
        },
        .layout = m_pipelineLayout
    };
    m_markPipeline = m_context.getDevice().createComputePipeline(nullptr, markPipelineInfo).value;
    m_context.getDevice().destroyShaderModule(markModule);


    // --- Compile Remap Shader ---
    std::string remapShaderSource = R"(
#version 460
layout(local_size_x = 256) in;
layout(push_constant) uniform PC {
    uint oldVoxelCount;
    uint newVoxelCount;
    uint fieldCount;
} pc;

// Bindings for old and new grids would be complex with buffer references
// For this implementation, we'll assume a simplified 1:1 mapping or nearest neighbor
// and that we are remapping a single field for demonstration.
// In a real system, we'd loop over all fields.

layout(std430, binding = 0) buffer OldGrid {
    float oldData[];
};

layout(std430, binding = 1) buffer NewGrid {
    float newData[];
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.newVoxelCount) return;

    // Simple nearest neighbor: map index directly if within bounds
    // Real implementation requires coordinate transformation
    if (idx < pc.oldVoxelCount) {
        newData[idx] = oldData[idx];
    } else {
        newData[idx] = 0.0; // Initialize new cells
    }
}
)";
    // Compile logic
    uuid = "remap_" + std::to_string(std::rand());
    glslFile = "/tmp/" + uuid + ".comp";
    spvFile = "/tmp/" + uuid + ".spv";
    {
        std::ofstream out(glslFile);
        out << remapShaderSource;
    }
    std::system(("/opt/homebrew/bin/glslc -fshader-stage=compute -o " + spvFile + " " + glslFile).c_str());
    std::remove(glslFile.c_str());

    std::vector<uint32_t> remapSpirv;
    {
        std::ifstream in(spvFile, std::ios::binary | std::ios::ate);
        if (in) {
            size_t size = in.tellg();
            remapSpirv.resize(size/4);
            in.seekg(0);
            in.read((char*)remapSpirv.data(), size);
        }
    }
    std::remove(spvFile.c_str());

    vk::ShaderModuleCreateInfo remapModuleInfo{
        .codeSize = remapSpirv.size() * 4,
        .pCode = remapSpirv.data()
    };
    vk::ShaderModule remapModule = m_context.getDevice().createShaderModule(remapModuleInfo);

    vk::ComputePipelineCreateInfo remapPipelineInfo{
        .stage = {
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = remapModule,
            .pName = "main"
        },
        .layout = m_pipelineLayout
    };
    m_remapPipeline = m_context.getDevice().createComputePipeline(nullptr, remapPipelineInfo).value;
    m_context.getDevice().destroyShaderModule(remapModule);

    LOG_DEBUG("Refinement pipelines created");
}

void refinement::RefinementManager::destroyPipelines()
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

void refinement::RefinementManager::allocateMaskBuffer(uint32_t voxelCount)
{
    LOG_DEBUG("Allocating mask buffer for {} voxels", voxelCount);

    if (voxelCount == 0) {
        LOG_WARN("Attempting to allocate mask buffer with 0 voxels");
        return;
    }

    // Allocate GPU-side mask buffer
    uint64_t maskSize = static_cast<uint64_t>(voxelCount) * sizeof(uint8_t);

    m_maskBuffer = m_allocator.createBuffer(
        maskSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst);

    // Allocate host-visible staging buffer for readback
    m_maskStagingBuffer = m_allocator.createBuffer(
        maskSize,
        vk::BufferUsageFlagBits::eTransferDst);

    LOG_DEBUG("Mask buffer allocated: {} bytes", maskSize);
}

void refinement::RefinementManager::markCells(vk::CommandBuffer cmd,
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

bool refinement::RefinementManager::rebuildTopology(
    nanovdb_adapter::GpuGridManager& gridMgr)
{
    LOG_INFO("Rebuilding grid topology");

    // Download mask from GPU
    auto device = m_context.getDevice();

    auto copyCmd = m_context.beginSingleTimeCommands();

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

void refinement::RefinementManager::remapFields(
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
// Add to end of RefinementManager.cpp before closing namespace

void refinement::RefinementManager::allocateLevelBuffer(uint32_t voxelCount) {
    LOG_DEBUG("Allocating level buffer for {} voxels", voxelCount);
    
    // Allocate GPU buffer
    m_levelBuffer = m_allocator.allocateBuffer(
        voxelCount * sizeof(uint8_t),
        vk::BufferUsageFlagBits::eStorageBuffer | 
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eTransferSrc,
        vma::MemoryUsage::eGpuOnly,
        "LevelBuffer");
    
    // Initialize host-side levels to 0 (base level)
    m_hostLevels.resize(voxelCount, 0);
    
    // Upload to GPU
    auto stagingBuffer = m_allocator.allocateBuffer(
        voxelCount * sizeof(uint8_t),
        vk::BufferUsageFlagBits::eTransferSrc,
        vma::MemoryUsage::eCpuToGpu,
        "LevelStagingBuffer");
    
    uint8_t* ptr = m_allocator.mapBuffer(stagingBuffer);
    std::memcpy(ptr, m_hostLevels.data(), voxelCount);
    m_allocator.unmapBuffer(stagingBuffer);
    
    // Copy to GPU
    auto cmd = m_context.beginSingleTimeCommands();
    vk::BufferCopy region{};
    region.size = voxelCount * sizeof(uint8_t);
    cmd.copyBuffer(stagingBuffer.buffer, m_levelBuffer.buffer, region);
    m_context.endOneTimeCommand(cmd);
    
    m_allocator.freeBuffer(stagingBuffer);
    
    LOG_DEBUG("Level buffer allocated and initialized");
}

void refinement::RefinementManager::updateLevels(
    const std::vector<nanovdb::Coord>& newLUT,
    const std::vector<nanovdb::Coord>& oldLUT)
{
    LOG_DEBUG("Updating refinement levels ({} -> {})", oldLUT.size(), newLUT.size());
    
    // Build old coord → index map for fast lookup
    std::unordered_map<nanovdb::Coord, size_t> oldCoordMap;
    for (size_t i = 0; i < oldLUT.size(); i++) {
        oldCoordMap[oldLUT[i]] = i;
    }
    
    // Resize host levels
    std::vector<uint8_t> newLevels(newLUT.size(), 0);
    
    for (size_t i = 0; i < newLUT.size(); i++) {
        nanovdb::Coord coord = newLUT[i];
        
        // Check if this is an exact match (kept voxel)
        auto it = oldCoordMap.find(coord);
        if (it != oldCoordMap.end()) {
            // Same voxel, keep level
            newLevels[i] = m_hostLevels[it->second];
            continue;
        }
        
        // Check if this is a child (refined voxel)
        nanovdb::Coord parent(coord[0] / 2, coord[1] / 2, coord[2] / 2);
        auto parentIt = oldCoordMap.find(parent);
        if (parentIt != oldCoordMap.end()) {
            // Child of existing voxel, increment level
            newLevels[i] = m_hostLevels[parentIt->second] + 1;
            continue;
        }
        
        // Check if this is a parent (coarsened voxel)
        nanovdb::Coord child0 = parent * 2;
        auto childIt = oldCoordMap.find(child0);
        if (childIt != oldCoordMap.end()) {
            // Parent of existing voxel, decrement level
            newLevels[i] = (m_hostLevels[childIt->second] > 0) ? 
                           m_hostLevels[childIt->second] - 1 : 0;
            continue;
        }
        
        // New voxel with no relationship, set to base level
        newLevels[i] = 0;
    }
    
    // Update host-side storage
    m_hostLevels = std::move(newLevels);
    
    // Reallocate GPU buffer if size changed
    if (newLUT.size() != oldLUT.size()) {
        m_allocator.freeBuffer(m_levelBuffer);
        allocateLevelBuffer(newLUT.size());
    } else {
        // Upload updated levels
        auto stagingBuffer = m_allocator.allocateBuffer(
            m_hostLevels.size() * sizeof(uint8_t),
            vk::BufferUsageFlagBits::eTransferSrc,
            vma::MemoryUsage::eCpuToGpu,
            "LevelStagingBuffer");
        
        uint8_t* ptr = m_allocator.mapBuffer(stagingBuffer);
        std::memcpy(ptr, m_hostLevels.data(), m_hostLevels.size());
        m_allocator.unmapBuffer(stagingBuffer);
        
        auto cmd = m_context.beginSingleTimeCommands();
        vk::BufferCopy region{};
        region.size = m_hostLevels.size() * sizeof(uint8_t);
        cmd.copyBuffer(stagingBuffer.buffer, m_levelBuffer.buffer, region);
        m_context.endOneTimeCommand(cmd);
        
        m_allocator.freeBuffer(stagingBuffer);
    }
    
    LOG_DEBUG("Levels updated");
}
