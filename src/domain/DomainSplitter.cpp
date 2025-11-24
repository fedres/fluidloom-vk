#include "domain/DomainSplitter.hpp"
#include "core/Logger.hpp"

#include <nanovdb/util/GridBuilder.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace domain {

uint64_t DomainSplitter::getMortonCode(const nanovdb::Coord& coord) {
    // Compute bit-interleaved Morton code for 3D coordinates
    auto expandBits = [](uint32_t v) -> uint64_t {
        uint64_t x = v;
        x = (x | (x << 16)) & 0x030000FF;
        x = (x | (x << 8)) & 0x0300F00F;
        x = (x | (x << 4)) & 0x030C30C3;
        x = (x | (x << 2)) & 0x09249249;
        return x;
    };

    uint32_t x = static_cast<uint32_t>(coord[0] & 0xFFFFFFFF);
    uint32_t y = static_cast<uint32_t>(coord[1] & 0xFFFFFFFF);
    uint32_t z = static_cast<uint32_t>(coord[2] & 0xFFFFFFFF);

    return expandBits(x) | (expandBits(y) << 1) | (expandBits(z) << 2);
}

DomainSplitter::DomainSplitter(const SplitConfig& config)
    : m_config(config) {
    LOG_DEBUG("DomainSplitter initialized with {} GPUs", m_config.gpuCount);
}

std::vector<SubDomain> DomainSplitter::split(
    const nanovdb::GridHandle<nanovdb::HostBuffer>& grid) {
    LOG_INFO("Starting domain split for {} GPUs", m_config.gpuCount);

    auto* hostGrid = grid.grid();
    LOG_CHECK(hostGrid != nullptr, "Host grid is null");

    // Single GPU case
    if (m_config.gpuCount == 1) {
        SubDomain domain;
        domain.gpuIndex = 0;
        domain.bounds = hostGrid->gridMetaData()->indexBBox();

        // Collect all leaf nodes
        auto tree = hostGrid->tree();
        for (auto it = tree.beginLeaf(); it; ++it) {
            domain.assignedLeaves.push_back(it->bbox());
        }

        // Count active voxels
        for (auto it = tree.beginValueOn(); it; ++it) {
            domain.activeVoxelCount++;
        }

        LOG_INFO("Single GPU domain: {} active voxels", domain.activeVoxelCount);
        return {domain};
    }

    // Multi-GPU case: collect and sort leaf nodes
    std::vector<std::pair<nanovdb::CoordBBox, uint64_t>> leafBoxesWithMorton;
    auto tree = hostGrid->tree();

    LOG_DEBUG("Collecting leaf nodes...");
    for (auto it = tree.beginLeaf(); it; ++it) {
        nanovdb::CoordBBox leafBox = it->bbox();
        uint64_t mortonCode = getMortonCode(leafBox.min());
        leafBoxesWithMorton.push_back({leafBox, mortonCode});
    }

    // Sort by Morton code for spatial locality
    LOG_DEBUG("Sorting {} leaf nodes by Morton code", leafBoxesWithMorton.size());
    std::sort(leafBoxesWithMorton.begin(), leafBoxesWithMorton.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    // Count total active voxels
    uint64_t totalVoxels = hostGrid->activeVoxelCount();
    LOG_INFO("Total active voxels: {}", totalVoxels);

    // Load balancing: partition leaves to balance voxel distribution
    uint64_t targetPerGPU = totalVoxels / m_config.gpuCount;
    std::vector<SubDomain> domains(m_config.gpuCount);

    uint64_t currentCount = 0;
    uint32_t currentGpu = 0;
    nanovdb::CoordBBox currentBounds;
    bool boundsInitialized = false;

    LOG_DEBUG("Target voxels per GPU: {}", targetPerGPU);

    for (const auto& [leafBox, mortonCode] : leafBoxesWithMorton) {
        if (!boundsInitialized) {
            currentBounds = leafBox;
            boundsInitialized = true;
        } else {
            currentBounds.expand(leafBox);
        }

        domains[currentGpu].assignedLeaves.push_back(leafBox);
        currentCount += leafBox.volume();

        // Check if we should move to next GPU
        bool isLastLeaf = (&leafBox == &leafBoxesWithMorton.back().first);
        if ((currentCount >= targetPerGPU && currentGpu < m_config.gpuCount - 1) ||
            isLastLeaf) {
            // Finalize current domain
            domains[currentGpu].gpuIndex = currentGpu;
            domains[currentGpu].bounds = currentBounds;

            // Count active voxels in this domain
            uint32_t domainVoxels = 0;
            for (auto it = tree.beginValueOn(); it; ++it) {
                if (currentBounds.isInside(it.getCoord())) {
                    domainVoxels++;
                }
            }
            domains[currentGpu].activeVoxelCount = domainVoxels;

            LOG_DEBUG("Domain {}: {} leaves, {} voxels",
                     currentGpu, domains[currentGpu].assignedLeaves.size(), domainVoxels);

            if (!isLastLeaf) {
                // Reset for next GPU
                currentCount = 0;
                currentGpu++;
                boundsInitialized = false;
            }
        }
    }

    // Trim unused domains
    domains.erase(
        std::remove_if(domains.begin(), domains.end(),
                      [](const SubDomain& d) { return d.assignedLeaves.empty(); }),
        domains.end());

    // Compute neighbors for halo exchange
    computeNeighbors(domains);

    // Analyze load balance
    LoadBalanceStats stats = analyzeBalance(domains);
    LOG_INFO("Load balance: min={}, max={}, avg={:.1f}, imbalance={:.2f}x",
             stats.minVoxels, stats.maxVoxels, stats.averageVoxels, stats.imbalanceFactor);

    return domains;
}

nanovdb::GridHandle<nanovdb::HostBuffer> DomainSplitter::extract(
    const nanovdb::GridHandle<nanovdb::HostBuffer>& fullGrid,
    const SubDomain& domain) {
    LOG_DEBUG("Extracting sub-grid for domain {}", domain.gpuIndex);

    auto* hostGrid = fullGrid.grid();
    auto tree = hostGrid->tree();

    // Get grid value type
    nanovdb::GridType gridType = hostGrid->gridType();

    // Create builder for sub-grid
    nanovdb::GridBuilder<float> builder(0.0f);

    // Iterate all active voxels in full grid
    for (auto it = tree.beginValueOn(); it; ++it) {
        nanovdb::Coord coord = it.getCoord();

        // Include if voxel is within domain bounds
        if (domain.bounds.isInside(coord)) {
            builder.setValue(coord, static_cast<float>(*it));
        }
    }

    auto subHandle = builder.getHandle();
    LOG_DEBUG("Sub-grid extracted: {} active voxels",
              subHandle.grid()->activeVoxelCount());

    return subHandle;
}

void DomainSplitter::computeNeighbors(std::vector<SubDomain>& domains) {
    LOG_DEBUG("Computing neighbor relationships...");

    for (uint32_t i = 0; i < domains.size(); i++) {
        for (uint32_t j = i + 1; j < domains.size(); j++) {
            const auto& boxA = domains[i].bounds;
            const auto& boxB = domains[j].bounds;

            // Check if domains share a face
            int32_t dx = 0, dy = 0, dz = 0;

            // Check X direction
            if (boxA.max()[0] + 1 == boxB.min()[0]) {
                dx = 1;  // A is left of B
            } else if (boxB.max()[0] + 1 == boxA.min()[0]) {
                dx = -1; // B is left of A
            }

            // Check Y direction
            if (boxA.max()[1] + 1 == boxB.min()[1]) {
                dy = 1;
            } else if (boxB.max()[1] + 1 == boxA.min()[1]) {
                dy = -1;
            }

            // Check Z direction
            if (boxA.max()[2] + 1 == boxB.min()[2]) {
                dz = 1;
            } else if (boxB.max()[2] + 1 == boxA.min()[2]) {
                dz = -1;
            }

            // If domains are adjacent
            if ((dx != 0 || dy != 0 || dz != 0) &&
                (dx * dx + dy * dy + dz * dz) <= 2) { // Face or edge neighbor
                // Determine which face (only face neighbors for halo exchange)
                if (dx == 1 && dy == 0 && dz == 0) {
                    domains[i].neighbors.push_back({j, 1}); // +X
                    domains[j].neighbors.push_back({i, 0}); // -X
                } else if (dx == -1 && dy == 0 && dz == 0) {
                    domains[i].neighbors.push_back({j, 0}); // -X
                    domains[j].neighbors.push_back({i, 1}); // +X
                } else if (dy == 1 && dx == 0 && dz == 0) {
                    domains[i].neighbors.push_back({j, 3}); // +Y
                    domains[j].neighbors.push_back({i, 2}); // -Y
                } else if (dy == -1 && dx == 0 && dz == 0) {
                    domains[i].neighbors.push_back({j, 2}); // -Y
                    domains[j].neighbors.push_back({i, 3}); // +Y
                } else if (dz == 1 && dx == 0 && dy == 0) {
                    domains[i].neighbors.push_back({j, 5}); // +Z
                    domains[j].neighbors.push_back({i, 4}); // -Z
                } else if (dz == -1 && dx == 0 && dy == 0) {
                    domains[i].neighbors.push_back({j, 4}); // -Z
                    domains[j].neighbors.push_back({i, 5}); // +Z
                }
            }
        }
    }
}

DomainSplitter::LoadBalanceStats DomainSplitter::analyzeBalance(
    const std::vector<SubDomain>& domains) const {
    if (domains.empty()) {
        return LoadBalanceStats{};
    }

    std::vector<uint32_t> voxelCounts;
    for (const auto& domain : domains) {
        voxelCounts.push_back(domain.activeVoxelCount);
    }

    LoadBalanceStats stats;
    stats.minVoxels = *std::min_element(voxelCounts.begin(), voxelCounts.end());
    stats.maxVoxels = *std::max_element(voxelCounts.begin(), voxelCounts.end());

    uint64_t sum = std::accumulate(voxelCounts.begin(), voxelCounts.end(), 0UL);
    stats.averageVoxels = static_cast<double>(sum) / domains.size();

    // Standard deviation
    double sumSquares = 0.0;
    for (uint32_t count : voxelCounts) {
        double diff = count - stats.averageVoxels;
        sumSquares += diff * diff;
    }
    stats.standardDeviation = std::sqrt(sumSquares / domains.size());

    // Imbalance factor
    stats.imbalanceFactor = (stats.averageVoxels > 0)
        ? static_cast<double>(stats.maxVoxels) / stats.averageVoxels
        : 1.0;

    return stats;
}

} // namespace domain
