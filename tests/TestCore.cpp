#include "VulkanFixture.hpp"
#include "core/Logger.hpp"
#include "nanovdb_adapter/GridLoader.hpp"
#include "field/FieldRegistry.hpp"

#include <catch2/catch_all.hpp>

/**
 * Test Suite: Core Infrastructure
 *
 * Verifies basic functionality of Vulkan context, memory allocation,
 * and logging system.
 */
TEST_CASE_METHOD(VulkanFixture, "Logger initialization", "[core][logger]")
{
    LOG_INFO("Logger test message");
    LOG_DEBUG("Debug message");
    LOG_WARN("Warning message");
    LOG_ERROR("Error message");

    // If we reach here without crash, logger works
    REQUIRE(true);
}

TEST_CASE_METHOD(VulkanFixture, "Vulkan context creation", "[core][vulkan]")
{
    auto& ctx = getContext();

    REQUIRE(ctx.getInstance());
    REQUIRE(ctx.getPhysicalDevice());
    REQUIRE(ctx.getDevice());
    REQUIRE(ctx.getComputeQueue());
    REQUIRE(ctx.getComputeQueueFamily() >= 0);
}

TEST_CASE_METHOD(VulkanFixture, "Memory allocator initialization", "[core][memory]")
{
    auto& alloc = getAllocator();
    auto& ctx = getContext();

    // Allocate a small buffer
    auto buf = alloc.allocateBuffer(
        256,
        vk::BufferUsageFlagBits::eUniformBuffer,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        "TestBuffer");

    REQUIRE(buf.handle);
    REQUIRE(buf.size == 256);
    REQUIRE(buf.allocation);

    // Test mapping
    float* data = static_cast<float*>(alloc.mapBuffer(buf));
    REQUIRE(data != nullptr);

    // Write test data
    data[0] = 3.14f;

    alloc.unmapBuffer(buf);

    // Read back
    float* readData = static_cast<float*>(alloc.mapBuffer(buf));
    REQUIRE(readData[0] == Catch::Approx(3.14f));
    alloc.unmapBuffer(buf);

    // Cleanup
    alloc.freeBuffer(buf);
}

TEST_CASE_METHOD(VulkanFixture, "Buffer allocation and readback", "[core][memory]")
{
    auto& alloc = getAllocator();

    const std::vector<float> testData{1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

    // Allocate GPU buffer
    auto gpuBuf = alloc.allocateBuffer(
        testData.size() * sizeof(float),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        VMA_MEMORY_USAGE_GPU_ONLY,
        "TestGPUBuffer");

    // Upload via staging buffer
    auto stagingBuf = alloc.allocateBuffer(
        testData.size() * sizeof(float),
        vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        "TestStagingBuffer");

    float* stagingPtr = static_cast<float*>(alloc.mapBuffer(stagingBuf));
    std::memcpy(stagingPtr, testData.data(), testData.size() * sizeof(float));
    alloc.unmapBuffer(stagingBuf);

    // Copy to GPU
    auto cmd = beginCommand();
    vk::BufferCopy region(0, 0, testData.size() * sizeof(float));
    cmd.copyBuffer(stagingBuf.handle, gpuBuf.handle, 1, &region);
    endCommand(cmd);

    // Download back to CPU (via another staging buffer)
    auto readbackBuf = alloc.allocateBuffer(
        testData.size() * sizeof(float),
        vk::BufferUsageFlagBits::eTransferDst,
        VMA_MEMORY_USAGE_GPU_TO_CPU,
        "TestReadbackBuffer");

    cmd = beginCommand();
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = testData.size() * sizeof(float);
    cmd.copyBuffer(gpuBuf.handle, readbackBuf.handle, 1, &region);
    endCommand(cmd);

    // Verify data
    float* readPtr = static_cast<float*>(alloc.mapBuffer(readbackBuf));
    for (size_t i = 0; i < testData.size(); ++i) {
        REQUIRE(readPtr[i] == Catch::Approx(testData[i]));
    }
    alloc.unmapBuffer(readbackBuf);

    // Cleanup
    alloc.freeBuffer(gpuBuf);
    alloc.freeBuffer(stagingBuf);
    alloc.freeBuffer(readbackBuf);
}

/**
 * Test Suite: NanoVDB Integration
 */
TEST_CASE_METHOD(VulkanFixture, "Grid creation", "[nanovdb][grid]")
{
    auto grid = createTestGrid(8, 1.0f);

    REQUIRE(grid);

    auto* gridPtr = grid.grid<float>();
    REQUIRE(gridPtr != nullptr);
    REQUIRE(gridPtr->activeVoxelCount() == 8 * 8 * 8);
}

TEST_CASE_METHOD(VulkanFixture, "Gradient grid creation", "[nanovdb][grid]")
{
    auto grid = createGradientTestGrid(8);

    REQUIRE(grid);

    auto* gridPtr = grid.grid<float>();
    REQUIRE(gridPtr != nullptr);
    REQUIRE(gridPtr->activeVoxelCount() > 0);

    // Verify some values
    auto value00 = gridPtr->tree().getValue(nanovdb::Coord(0, 0, 0));
    REQUIRE(value00 == Catch::Approx(0.0f));
}

/**
 * Test Suite: Field Registry
 */
TEST_CASE_METHOD(VulkanFixture, "Field registration", "[field][registry]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    // Create field registry with 1024 active voxels
    field::FieldRegistry registry(ctx, alloc, 1024);

    // Register a field
    auto& field = registry.registerField("density", vk::Format::eR32Sfloat);

    // Verify it exists
    REQUIRE(!field.name.empty());
    REQUIRE(field.format == vk::Format::eR32Sfloat);
}

TEST_CASE_METHOD(VulkanFixture, "Multiple field registration", "[field][registry]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    // Create field registry
    field::FieldRegistry registry(ctx, alloc, 1024);

    // Register multiple fields
    auto& density = registry.registerField("density", vk::Format::eR32Sfloat);
    auto& velocity = registry.registerField("velocity", vk::Format::eR32G32B32Sfloat);
    auto& pressure = registry.registerField("pressure", vk::Format::eR32Sfloat);

    REQUIRE(registry.getFieldCount() == 3);
    REQUIRE(density.name == "density");
    REQUIRE(velocity.name == "velocity");
    REQUIRE(pressure.name == "pressure");
}

TEST_CASE_METHOD(VulkanFixture, "Field lookup by name", "[field][registry]")
{
    auto& ctx = getContext();
    auto& alloc = getAllocator();

    // Create field registry
    field::FieldRegistry registry(ctx, alloc, 1024);

    auto& density = registry.registerField("density", vk::Format::eR32Sfloat);
    auto& velocity = registry.registerField("velocity", vk::Format::eR32G32B32Sfloat);

    // Verify fields can be looked up by name
    REQUIRE(registry.hasField("density"));
    REQUIRE(registry.hasField("velocity"));
    REQUIRE(!registry.hasField("pressure"));

    const auto& densityRef = registry.getField("density");
    const auto& velocityRef = registry.getField("velocity");
    REQUIRE(densityRef.name == "density");
    REQUIRE(velocityRef.name == "velocity");
}

/**
 * Test Suite: Utility Helpers
 */
TEST_CASE("Buffer comparison", "[test][helpers]")
{
    std::vector<float> a{1.0f, 2.0f, 3.0f};
    std::vector<float> b{1.0f, 2.0f, 3.0f};
    std::vector<float> c{1.0f, 2.0f, 4.0f};

    REQUIRE(VulkanFixture::buffersEqual(a, b));
    REQUIRE_FALSE(VulkanFixture::buffersEqual(a, c));
}

TEST_CASE("Buffer size mismatch", "[test][helpers]")
{
    std::vector<float> a{1.0f, 2.0f, 3.0f};
    std::vector<float> b{1.0f, 2.0f};

    REQUIRE_FALSE(VulkanFixture::buffersEqual(a, b));
}

TEST_CASE("Tolerance comparison", "[test][helpers]")
{
    std::vector<float> a{1.0f, 2.0f, 3.0f};
    std::vector<float> b{1.0f + 1e-6f, 2.0f + 1e-6f, 3.0f + 1e-6f};
    std::vector<float> c{1.0f + 1e-4f, 2.0f + 1e-4f, 3.0f + 1e-4f};

    REQUIRE(VulkanFixture::buffersEqual(a, b, 1e-5f));
    REQUIRE_FALSE(VulkanFixture::buffersEqual(a, c, 1e-5f));
}
