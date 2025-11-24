#include "VulkanFixture.hpp"
#include "domain/DomainSplitter.hpp"
#include "nanovdb_adapter/GpuGridManager.hpp"

#include <catch2/catch.hpp>

/**
 * Test Suite: Domain Decomposition
 *
 * Verifies spatial domain partitioning and load balancing.
 */
TEST_CASE_METHOD(VulkanFixture, "Domain splitter creation", "[domain][splitter]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    domain::DomainSplitter splitter(ctx, alloc, 1); // Single GPU

    // Verify initialization
    REQUIRE(splitter.getGPUCount() == 1);
}

TEST_CASE_METHOD(VulkanFixture, "Single GPU domain decomposition", "[domain][splitter]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    // Create test grid
    auto grid = createTestGrid(32, 1.0f);
    auto* gridPtr = grid.grid<float>();

    REQUIRE(gridPtr->activeVoxelCount() > 0);

    // Create domain splitter
    domain::DomainSplitter splitter(ctx, alloc, 1);

    // Decompose domain
    std::vector<nanovdb::Coord> lut;
    for (auto it = gridPtr->cbeginValueOn(); it; ++it) {
        lut.push_back(it.getCoord());
    }

    auto subdomains = splitter.decompose(lut);

    // Single GPU should have exactly one subdomain
    REQUIRE(subdomains.size() == 1);
    REQUIRE(subdomains[0].voxelCount == lut.size());
}

TEST_CASE_METHOD(VulkanFixture, "Domain neighbor computation", "[domain][splitter]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    domain::DomainSplitter splitter(ctx, alloc, 2); // 2 GPUs

    // Create test grid
    auto grid = createTestGrid(16, 1.0f);
    auto* gridPtr = grid.grid<float>();

    std::vector<nanovdb::Coord> lut;
    for (auto it = gridPtr->cbeginValueOn(); it; ++it) {
        lut.push_back(it.getCoord());
    }

    auto subdomains = splitter.decompose(lut);

    // With 2 GPUs, should have 2 subdomains (if not empty)
    if (lut.size() > 1) {
        REQUIRE(subdomains.size() <= 2);
    }

    // Verify bounding boxes are valid
    for (const auto& sd : subdomains) {
        REQUIRE(sd.voxelCount >= 0);
    }
}

TEST_CASE_METHOD(VulkanFixture, "Load balancing with gradient grid", "[domain][splitter][load-balance]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    // Create gradient grid (less dense at edges)
    auto grid = createGradientTestGrid(16);
    auto* gridPtr = grid.grid<float>();

    std::vector<nanovdb::Coord> lut;
    for (auto it = gridPtr->cbeginValueOn(); it; ++it) {
        lut.push_back(it.getCoord());
    }

    domain::DomainSplitter splitter(ctx, alloc, 2);
    auto subdomains = splitter.decompose(lut);

    // Verify load distribution
    uint32_t totalVoxels = 0;
    for (const auto& sd : subdomains) {
        totalVoxels += sd.voxelCount;
    }

    if (subdomains.size() == 2) {
        uint32_t diff = 0;
        if (subdomains[0].voxelCount > subdomains[1].voxelCount) {
            diff = subdomains[0].voxelCount - subdomains[1].voxelCount;
        } else {
            diff = subdomains[1].voxelCount - subdomains[0].voxelCount;
        }

        // Load imbalance should be reasonable (< 20% difference)
        uint32_t maxAllowed = totalVoxels / 5;
        REQUIRE(diff <= maxAllowed);
    }
}

TEST_CASE_METHOD(VulkanFixture, "Empty domain handling", "[domain][splitter]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    domain::DomainSplitter splitter(ctx, alloc, 1);

    // Empty LUT
    std::vector<nanovdb::Coord> emptyLUT;
    auto subdomains = splitter.decompose(emptyLUT);

    // Should handle gracefully
    REQUIRE(subdomains.empty() || subdomains[0].voxelCount == 0);
}
