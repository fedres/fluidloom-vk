#include "nanovdb_adapter/GpuGridManager.hpp"
#include "core/VulkanContext.hpp"
#include "core/Logger.hpp"

#include <nanovdb/NodeManager.h>
#include <algorithm>

namespace nanovdb_adapter {

// Morton code computation using bit-interleaving
uint64_t GpuGridManager::getMortonCode(uint32_t x, uint32_t y, uint32_t z) {
    // Expand bits: 00000xxx -> 00x00x00x00
    auto expandBits = [](uint32_t v) -> uint64_t {
        uint64_t x = v;
        x = (x | (x << 16)) & 0x030000FF;
        x = (x | (x << 8)) & 0x0300F00F;
        x = (x | (x << 4)) & 0x030C30C3;
        x = (x | (x << 2)) & 0x09249249;
        return x;
    };

    return expandBits(x) | (expandBits(y) << 1) | (expandBits(z) << 2);
}

GpuGridManager::GpuGridManager(const core::VulkanContext& context,
                             core::MemoryAllocator& allocator)
    : m_context(context), m_allocator(allocator) {
    LOG_DEBUG("GpuGridManager initialized");
}

GpuGridManager::GridResources GpuGridManager::upload(
    const nanovdb::GridHandle<nanovdb::HostBuffer>& grid) {
    LOG_INFO("Uploading NanoVDB grid to GPU...");

    auto* hostGrid = grid.grid<float>();
    LOG_CHECK(hostGrid != nullptr, "Host grid is null or not a float grid");

    // Get grid bounds
    nanovdb::CoordBBox gridBounds = hostGrid->indexBBox();
    LOG_DEBUG("Grid bounds: [{},{},{}] to [{},{},{}]",
              gridBounds.min()[0], gridBounds.min()[1], gridBounds.min()[2],
              gridBounds.max()[0], gridBounds.max()[1], gridBounds.max()[2]);

    // Step 1: Collect active coordinates and values in Morton order
    std::vector<nanovdb::Coord> activeCoords;
    std::vector<float> activeValues;

    LOG_DEBUG("Collecting active voxels...");
    
    // Use NodeManager for efficient iteration
    auto mgrHandle = nanovdb::createNodeManager(*hostGrid);
    auto* mgr = mgrHandle.mgr<float>();
    
    if (mgr) {
        for (uint32_t i = 0; i < mgr->leafCount(); ++i) {
            const auto& leaf = mgr->leaf(i);
            for (auto it = leaf.valueMask().beginOn(); it; ++it) {
                nanovdb::Coord ijk = leaf.offsetToGlobalCoord(*it);
                float value = leaf.getValue(*it);
                activeCoords.push_back(ijk);
                activeValues.push_back(value);
            }
        }
    }

    uint32_t activeVoxelCount = static_cast<uint32_t>(activeCoords.size());
    LOG_INFO("Found {} active voxels", activeVoxelCount);

    if (activeVoxelCount == 0) {
        throw std::runtime_error("Grid has no active voxels");
    }

    // Step 2: Sort by Morton code for spatial locality
    LOG_DEBUG("Sorting by Morton code...");
    std::vector<uint32_t> sortIndices(activeVoxelCount);
    std::iota(sortIndices.begin(), sortIndices.end(), 0);

    std::sort(sortIndices.begin(), sortIndices.end(),
        [&activeCoords](uint32_t a, uint32_t b) {
            const auto& coordA = activeCoords[a];
            const auto& coordB = activeCoords[b];
            return getMortonCode(coordA[0], coordA[1], coordA[2]) <
                   getMortonCode(coordB[0], coordB[1], coordB[2]);
        });

    // Reorder arrays by Morton order
    std::vector<nanovdb::Coord> sortedCoords(activeVoxelCount);
    std::vector<float> sortedValues(activeVoxelCount);
    for (uint32_t i = 0; i < activeVoxelCount; i++) {
        sortedCoords[i] = activeCoords[sortIndices[i]];
        sortedValues[i] = activeValues[sortIndices[i]];
    }

    // Step 3: Upload raw grid structure
    LOG_DEBUG("Uploading raw NanoVDB structure...");
    GridResources resources;
    resources.activeVoxelCount = activeVoxelCount;
    resources.bounds = gridBounds;

    size_t gridDataSize = grid.bufferSize();
    resources.rawGrid = m_allocator.createBuffer(
        gridDataSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eShaderDeviceAddress);

    m_allocator.uploadToGPU(resources.rawGrid, grid.data(), gridDataSize);

    // Step 4: Upload coordinate lookup table
    LOG_DEBUG("Uploading coordinate LUT...");
    size_t coordLutSize = activeVoxelCount * sizeof(nanovdb::Coord);
    resources.lutCoords = m_allocator.createBuffer(
        coordLutSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eShaderDeviceAddress);

    m_allocator.uploadToGPU(resources.lutCoords, sortedCoords.data(), coordLutSize);

    // Step 5: Upload linear values
    LOG_DEBUG("Uploading linear values...");
    size_t valuesSize = activeVoxelCount * sizeof(float);
    resources.linearValues = m_allocator.createBuffer(
        valuesSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eShaderDeviceAddress);

    m_allocator.uploadToGPU(resources.linearValues, sortedValues.data(), valuesSize);

    LOG_INFO("GPU grid upload complete. Total GPU memory: {} bytes",
             gridDataSize + coordLutSize + valuesSize);

    return resources;
}

void GpuGridManager::destroyGrid(GridResources& resources) {
    m_allocator.destroyBuffer(resources.rawGrid);
    m_allocator.destroyBuffer(resources.lutCoords);
    m_allocator.destroyBuffer(resources.linearValues);
    LOG_DEBUG("GPU grid resources destroyed");
}

} // namespace nanovdb_adapter
