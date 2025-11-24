#pragma once

#include <nanovdb/NanoVDB.h>
#include <vector>
#include <cstdint>

namespace domain {

/**
 * @brief Sub-domain descriptor for a single GPU
 */
struct SubDomain {
    uint32_t gpuIndex = 0;
    nanovdb::CoordBBox bounds;                           // Inclusive bounding box
    uint32_t activeVoxelCount = 0;
    std::vector<nanovdb::CoordBBox> assignedLeaves;     // Leaf nodes in this domain

    // Neighbor information for halo exchange
    struct Neighbor {
        uint32_t gpuIndex = 0;
        uint32_t face = 0;  // 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z
    };
    std::vector<Neighbor> neighbors;

    // Statistics
    uint64_t estimatedMemoryUsage() const {
        return static_cast<uint64_t>(activeVoxelCount) * sizeof(float) * 8;
    }
};

/**
 * @brief Domain decomposition for multi-GPU execution
 *
 * Splits a global NanoVDB grid into sub-domains aligned to leaf nodes,
 * with load balancing based on active voxel distribution.
 */
class DomainSplitter {
public:
    /**
     * @brief Configuration for domain splitting strategy
     */
    struct SplitConfig {
        uint32_t gpuCount = 1;
        uint32_t haloThickness = 2;
        bool preferSpatialLocality = true;
        float loadBalanceTolerance = 0.1f;
    };

    /**
     * @brief Load balance statistics
     */
    struct LoadBalanceStats {
        uint32_t minVoxels = 0;
        uint32_t maxVoxels = 0;
        double averageVoxels = 0.0;
        double standardDeviation = 0.0;
        double imbalanceFactor = 0.0;  // max / avg
    };

    explicit DomainSplitter(const SplitConfig& config = {});

    /**
     * Compute Morton code (Z-order curve) for a coordinate
     * @param coord 3D coordinate
     * @return Morton code
     */
    static uint64_t getMortonCode(const nanovdb::Coord& coord);

    /**
     * Main split function - divides grid into sub-domains
     * @param grid Full NanoVDB grid on host
     * @return Vector of sub-domains (one per GPU)
     */
    std::vector<SubDomain> split(const nanovdb::GridHandle<nanovdb::HostBuffer>& grid);

    /**
     * Extract sub-grid for a specific domain
     * @param fullGrid Full NanoVDB grid
     * @param domain Domain to extract
     * @return Sub-grid handle containing only voxels in domain bounds
     */
    nanovdb::GridHandle<nanovdb::HostBuffer> extract(
        const nanovdb::GridHandle<nanovdb::HostBuffer>& fullGrid,
        const SubDomain& domain);

    /**
     * Analyze load balance quality of a domain split
     * @param domains Vector of domains
     * @return Load balance statistics
     */
    LoadBalanceStats analyzeBalance(const std::vector<SubDomain>& domains) const;

private:
    SplitConfig m_config;

    // Find neighbors for halo exchange
    void computeNeighbors(std::vector<SubDomain>& domains);
};

} // namespace domain
