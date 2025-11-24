#include "refinement/TopologyRebuilder.hpp"
#include "core/Logger.hpp"

#include <nanovdb/GridBuilder.h>
#include <algorithm>
#include <cmath>

namespace refinement {

TopologyRebuilder::TopologyRebuilder(core::MemoryAllocator& allocator)
    : m_allocator(allocator)
{
    LOG_INFO("Initializing TopologyRebuilder");
}

nanovdb_adapter::GpuGridManager::GridResources
TopologyRebuilder::rebuildGrid(const std::vector<nanovdb::Coord>& oldLUT,
                                const std::vector<float>& oldValues,
                                const std::vector<uint8_t>& mask,
                                const nanovdb_adapter::GpuGridManager::GridResources& oldGridRes)
{
    LOG_INFO("Rebuilding grid topology from refinement mask");

    if (oldLUT.empty()) {
        LOG_ERROR("Old LUT is empty");
        throw std::runtime_error("Cannot rebuild from empty grid");
    }

    if (oldLUT.size() != oldValues.size()) {
        LOG_ERROR("LUT and values size mismatch");
        throw std::runtime_error("LUT and values must have same size");
    }

    if (oldLUT.size() != mask.size()) {
        LOG_ERROR("Mask size mismatch with grid");
        throw std::runtime_error("Mask must have same size as grid");
    }

    // Process refinement actions to generate new coordinates
    std::vector<nanovdb::Coord> newLUT = processRefinementActions(oldLUT, mask);

    LOG_DEBUG("Old grid voxels: {}, New grid voxels: {}",
              oldLUT.size(), newLUT.size());

    // Interpolate values at new coordinates
    std::vector<float> newValues;
    newValues.reserve(newLUT.size());

    for (const auto& coord : newLUT) {
        float value = interpolateValue(oldLUT, oldValues, coord);
        newValues.push_back(value);
    }

    // Build new NanoVDB grid
    std::vector<uint8_t> gridData = buildNanoVDBGrid(newLUT, newValues);

    // Create GridResources for new grid
    nanovdb_adapter::GpuGridManager::GridResources newGridRes;
    newGridRes.voxelCount = newLUT.size();
    newGridRes.memorySize = gridData.size();

    // Allocate GPU memory for new grid
    newGridRes.gridBuffer = m_allocator.allocateBuffer(
        gridData.size(),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vma::MemoryUsage::eGpuOnly,
        "RefinedGrid");

    // Upload grid to GPU
    auto stagingBuffer = m_allocator.allocateBuffer(
        gridData.size(),
        vk::BufferUsageFlagBits::eTransferSrc,
        vma::MemoryUsage::eCpuToGpu,
        "GridStagingBuffer");

    uint8_t* stagingPtr = m_allocator.mapBuffer(stagingBuffer);
    std::memcpy(stagingPtr, gridData.data(), gridData.size());
    m_allocator.unmapBuffer(stagingBuffer);

    // Copy to GPU
    auto cmd = m_allocator.getContext().beginOneTimeCommand();
    vk::BufferCopy region{};
    region.size = gridData.size();
    cmd.copyBuffer(stagingBuffer.buffer, newGridRes.gridBuffer.buffer, region);
    m_allocator.getContext().endOneTimeCommand(cmd);

    // Cleanup staging buffer
    m_allocator.freeBuffer(stagingBuffer);

    LOG_INFO("Grid topology rebuild complete");
    return newGridRes;
}

std::vector<nanovdb::Coord> TopologyRebuilder::processRefinementActions(
    const std::vector<nanovdb::Coord>& oldLUT,
    const std::vector<uint8_t>& mask)
{
    LOG_DEBUG("Processing refinement actions with sibling detection");

    std::vector<nanovdb::Coord> newCoords;
    newCoords.reserve(oldLUT.size() * 2);  // Estimate

    // Track which voxels have been processed (for coarsening)
    std::unordered_set<size_t> processed;

    // First pass: detect sibling groups for coarsening
    std::unordered_map<nanovdb::Coord, std::vector<size_t>> siblingGroups;
    
    for (size_t i = 0; i < oldLUT.size(); i++) {
        if (mask[i] == 2) {  // Coarsen
            nanovdb::Coord parent(
                oldLUT[i][0] / 2,
                oldLUT[i][1] / 2,
                oldLUT[i][2] / 2
            );
            siblingGroups[parent].push_back(i);
        }
    }

    // Process coarsening groups
    for (const auto& [parent, siblings] : siblingGroups) {
        if (siblings.size() == 8) {
            // All 8 siblings present and want to coarsen
            LOG_DEBUG("Coarsening 8 siblings to parent at ({}, {}, {})",
                     parent[0], parent[1], parent[2]);
            newCoords.push_back(parent);
            
            // Mark as processed
            for (size_t idx : siblings) {
                processed.insert(idx);
            }
        } else {
            // Not all siblings present, keep individual voxels
            LOG_DEBUG("Incomplete sibling group ({}/8), keeping voxels", siblings.size());
            for (size_t idx : siblings) {
                newCoords.push_back(oldLUT[idx]);
                processed.insert(idx);
            }
        }
    }

    // Second pass: process refinement and keep actions
    for (size_t i = 0; i < oldLUT.size(); i++) {
        if (processed.count(i) > 0) {
            continue;  // Already handled in coarsening
        }

        uint8_t action = mask[i];

        if (action == 1) {
            // Refine: create 8 children at 2x resolution
            nanovdb::Coord baseCoord = oldLUT[i] * 2;
            for (int dz = 0; dz < 2; dz++) {
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        newCoords.push_back(baseCoord + nanovdb::Coord(dx, dy, dz));
                    }
                }
            }
        } else if (action == 2) {
            // Coarsen: mark for removal (only keep parent)
            // In practice, check if all 8 siblings want to coarsen
            nanovdb::Coord parentCoord(coord.x() / 2, coord.y() / 2, coord.z() / 2);
            newCoords.push_back(parentCoord);
        }
    }

    // Remove duplicates (may occur from coarsening)
    std::sort(newCoords.begin(), newCoords.end(),
              [](const nanovdb::Coord& a, const nanovdb::Coord& b) {
                  if (a.x() != b.x()) return a.x() < b.x();
                  if (a.y() != b.y()) return a.y() < b.y();
                  return a.z() < b.z();
              });

    auto lastUnique = std::unique(newCoords.begin(), newCoords.end(),
                                  [](const nanovdb::Coord& a, const nanovdb::Coord& b) {
                                      return a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
                                  });

    newCoords.erase(lastUnique, newCoords.end());

    LOG_DEBUG("Refinement actions processed: {} new coordinates", newCoords.size());
    return newCoords;
}

std::vector<uint8_t> TopologyRebuilder::buildNanoVDBGrid(
    const std::vector<nanovdb::Coord>& coordinates,
    const std::vector<float>& values)
{
    LOG_DEBUG("Building NanoVDB grid from coordinates and values");

    if (coordinates.empty()) {
        LOG_WARN("Building grid from empty coordinate list");
        return std::vector<uint8_t>();
    }

    // Create grid builder
    nanovdb::GridBuilder<float> builder(0.0f);

    // Set values at all coordinates
    for (size_t i = 0; i < coordinates.size(); ++i) {
        builder.setValue(coordinates[i], values[i]);
    }

    // Finalize grid
    auto gridHandle = builder.getHandle<nanovdb::HostBuffer>();

    // Serialize grid to byte buffer
    std::vector<uint8_t> gridData;
    gridHandle.save("", nanovdb::Codec::Default, 0, nullptr, &gridData);

    LOG_DEBUG("NanoVDB grid built: {} bytes", gridData.size());
    return gridData;
}

std::vector<nanovdb::Coord> TopologyRebuilder::generateLUT(
    const nanovdb::GridHandle<nanovdb::HostBuffer>& handle)
{
    LOG_DEBUG("Generating lookup table from NanoVDB grid");

    std::vector<nanovdb::Coord> lut;

    if (!handle) {
        LOG_WARN("Invalid grid handle");
        return lut;
    }

    // Extract active voxel coordinates from grid
    auto* grid = handle.grid<float>();
    if (!grid) {
        LOG_WARN("Grid has no float data");
        return lut;
    }

    // Iterate over all active voxels
    for (auto it = grid->cbeginValueOn(); it; ++it) {
        lut.push_back(it.getCoord());
    }

    LOG_DEBUG("Generated LUT with {} coordinates", lut.size());
    return lut;
}

float TopologyRebuilder::interpolateValue(const std::vector<nanovdb::Coord>& oldLUT,
                                          const std::vector<float>& oldValues,
                                          const nanovdb::Coord& target) const
{
    // Find nearest neighbor(s) and interpolate
    // For simplicity, use nearest neighbor (0th order)

    float minDist = std::numeric_limits<float>::max();
    float nearestValue = 0.0f;

    for (size_t i = 0; i < oldLUT.size(); ++i) {
        float dx = static_cast<float>(target.x() - oldLUT[i].x());
        float dy = static_cast<float>(target.y() - oldLUT[i].y());
        float dz = static_cast<float>(target.z() - oldLUT[i].z());

        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < minDist) {
            minDist = dist;
            nearestValue = oldValues[i];
        }
    }

    return nearestValue;
}

bool TopologyRebuilder::validateGrid(
    const nanovdb_adapter::GpuGridManager::GridResources& gridRes) const
{
    LOG_DEBUG("Validating grid structure");

    if (gridRes.voxelCount == 0) {
        LOG_WARN("Grid has zero active voxels");
        return false;
    }

    if (gridRes.memorySize == 0) {
        LOG_WARN("Grid has zero memory");
        return false;
    }

    return true;
}

TopologyRebuilder::GridStats TopologyRebuilder::computeStats(
    const std::vector<nanovdb::Coord>& lut) const
{
    GridStats stats;
    stats.activeVoxelCount = lut.size();

    // Rough estimate of leaf nodes (NanoVDB leaves are 8^3 = 512 voxels)
    stats.leafCount = std::max(1u, stats.activeVoxelCount / 512);

    // Internal nodes
    stats.nodeCount = stats.leafCount / 8 + 1;

    return stats;
}

} // namespace refinement
