#pragma once

#include "core/VulkanContext.hpp"
#include "core/MemoryAllocator.hpp"
#include "nanovdb_adapter/GridLoader.hpp"
#include "nanovdb_adapter/GpuGridManager.hpp"
#include "field/FieldRegistry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nanovdb/GridBuilder.h>
#include <vector>
#include <memory>

/**
 * @brief Test fixture providing Vulkan context and utilities
 *
 * Automatically initializes and cleans up Vulkan resources for each test.
 * Provides helper methods for common testing tasks.
 */
class VulkanFixture {
public:
    /**
     * Initialize Vulkan context and allocator
     */
    VulkanFixture();

    /**
     * Cleanup resources
     */
    ~VulkanFixture();

    /**
     * Get Vulkan context
     */
    core::VulkanContext& getContext() { return *m_context; }

    /**
     * Get memory allocator
     */
    core::MemoryAllocator& getAllocator() { return *m_allocator; }

    /**
     * Create a simple test grid filled with constant value
     * @param size Grid dimension (creates size×size×size grid)
     * @param value Voxel value to fill
     * @return Grid handle
     */
    static nanovdb::GridHandle<nanovdb::HostBuffer> createTestGrid(
        uint32_t size = 16,
        float value = 1.0f);

    /**
     * Create a test grid with gradient values
     * @param size Grid dimension
     * @return Grid handle with values = (x + y + z) / (3*size)
     */
    static nanovdb::GridHandle<nanovdb::HostBuffer> createGradientTestGrid(
        uint32_t size = 16);

    /**
     * Compare two float buffers with tolerance
     * @param a First buffer
     * @param b Second buffer
     * @param epsilon Tolerance for floating-point comparison
     * @return True if all elements match within epsilon
     */
    static bool buffersEqual(const std::vector<float>& a,
                            const std::vector<float>& b,
                            float epsilon = 1e-5f);

    /**
     * Helper to log buffer contents (for debugging)
     * @param data Buffer to log
     * @param maxElements Maximum elements to display (0 = all)
     */
    static void logBuffer(const std::vector<float>& data,
                         size_t maxElements = 10);

protected:
    std::unique_ptr<core::VulkanContext> m_context;
    std::unique_ptr<core::MemoryAllocator> m_allocator;
    vk::CommandPool m_commandPool;

    /**
     * Begin a one-time command buffer
     */
    vk::CommandBuffer beginCommand();

    /**
     * End and submit a one-time command buffer
     */
    void endCommand(vk::CommandBuffer cmd);
};

// ============================================================================
// Inline Implementation
// ============================================================================

inline VulkanFixture::VulkanFixture()
{
    m_context = std::make_unique<core::VulkanContext>();
    m_allocator = std::make_unique<core::MemoryAllocator>(*m_context);

    // Create command pool for test operations
    auto result = m_context->getDevice().createCommandPool(
        vk::CommandPoolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = m_context->getComputeQueueFamily()});

    REQUIRE(result.result == vk::Result::eSuccess);
    m_commandPool = result.value;
}

inline VulkanFixture::~VulkanFixture()
{
    if (m_commandPool) {
        m_context->getDevice().destroyCommandPool(m_commandPool);
    }
}

inline vk::CommandBuffer VulkanFixture::beginCommand()
{
    auto allocResult = m_context->getDevice().allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{
            .commandPool = m_commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1});

    REQUIRE(allocResult.result == vk::Result::eSuccess);
    return allocResult.value[0];
}

inline void VulkanFixture::endCommand(vk::CommandBuffer cmd)
{
    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    auto submitResult = m_context->getComputeQueue().submit(submitInfo);
    REQUIRE(submitResult == vk::Result::eSuccess);

    auto waitResult = m_context->getComputeQueue().waitIdle();
    REQUIRE(waitResult == vk::Result::eSuccess);

    m_context->getDevice().freeCommandBuffers(m_commandPool, cmd);
}

inline nanovdb::GridHandle<nanovdb::HostBuffer>
VulkanFixture::createTestGrid(uint32_t size, float value)
{
    nanovdb::GridBuilder<float> builder(0.0f);

    for (uint32_t x = 0; x < size; ++x) {
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t z = 0; z < size; ++z) {
                builder.setValue(nanovdb::Coord(x, y, z), value);
            }
        }
    }

    return builder.getHandle<nanovdb::HostBuffer>();
}

inline nanovdb::GridHandle<nanovdb::HostBuffer>
VulkanFixture::createGradientTestGrid(uint32_t size)
{
    nanovdb::GridBuilder<float> builder(0.0f);

    float maxVal = 3.0f * size;
    for (uint32_t x = 0; x < size; ++x) {
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t z = 0; z < size; ++z) {
                float value = (x + y + z) / maxVal;
                builder.setValue(nanovdb::Coord(x, y, z), value);
            }
        }
    }

    return builder.getHandle<nanovdb::HostBuffer>();
}

inline bool VulkanFixture::buffersEqual(const std::vector<float>& a,
                                        const std::vector<float>& b,
                                        float epsilon)
{
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) >= epsilon) {
            return false;
        }
    }

    return true;
}

inline void VulkanFixture::logBuffer(const std::vector<float>& data,
                                     size_t maxElements)
{
    size_t count = (maxElements == 0) ? data.size() : std::min(maxElements, data.size());
    for (size_t i = 0; i < count; ++i) {
        printf("  [%zu] = %f\n", i, data[i]);
    }
    if (count < data.size()) {
        printf("  ... (%zu more elements)\n", data.size() - count);
    }
}
