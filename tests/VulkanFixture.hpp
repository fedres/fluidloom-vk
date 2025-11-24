#pragma once

// IMPORTANT: Include VulkanConfig FIRST to configure volk and Vulkan headers
#include "core/VulkanConfig.hpp"
#include "core/VulkanContext.hpp"
#include "core/Logger.hpp"
#include "core/MemoryAllocator.hpp"
#include "nanovdb_adapter/GridLoader.hpp"
#include "nanovdb_adapter/GpuGridManager.hpp"
#include "field/FieldRegistry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nanovdb/tools/GridBuilder.h>
#include <nanovdb/tools/CreateNanoGrid.h>
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
    // Initialize logger first
    core::Logger::init();
    
    try {
        // On macOS, ensure Vulkan can find MoltenVK ICD
        #ifdef __APPLE__
        const char* icdPath = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
        setenv("VK_ICD_FILENAMES", icdPath, 0);  // Don't overwrite if already set
        setenv("VK_DRIVER_FILES", icdPath, 0);
        #endif
        
        // Create Vulkan context in headless mode (no validation for faster tests)
        m_context = std::make_unique<core::VulkanContext>();
        m_context->init(false); // Disable validation for tests
        
        // Create memory allocator
        m_allocator = std::make_unique<core::MemoryAllocator>(*m_context);

        // Create command pool for test operations
        vk::CommandPoolCreateInfo poolInfo(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            m_context->getComputeQueueFamily());

        m_commandPool = m_context->getDevice().createCommandPool(poolInfo);
        
    } catch (const std::exception& e) {
        FAIL("Failed to initialize VulkanFixture: " << e.what());
    }
}

inline VulkanFixture::~VulkanFixture()
{
    if (m_commandPool) {
        m_context->getDevice().destroyCommandPool(m_commandPool);
    }
}

inline vk::CommandBuffer VulkanFixture::beginCommand()
{
    vk::CommandBufferAllocateInfo allocInfo(
        m_commandPool,
        vk::CommandBufferLevel::ePrimary,
        1);

    auto buffers = m_context->getDevice().allocateCommandBuffers(allocInfo);
    return buffers[0];
}

inline void VulkanFixture::endCommand(vk::CommandBuffer cmd)
{
    cmd.end();

    vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &cmd);
    m_context->getComputeQueue().submit(submitInfo);
    m_context->getComputeQueue().waitIdle();

    m_context->getDevice().freeCommandBuffers(m_commandPool, cmd);
}

inline nanovdb::GridHandle<nanovdb::HostBuffer>
VulkanFixture::createTestGrid(uint32_t size, float value)
{
    // Create a CPU-side grid using NanoVDB's build tools
    using SrcGridT = nanovdb::tools::build::FloatGrid;

    SrcGridT srcGrid(0.0f);  // background value
    auto acc = srcGrid.getAccessor();

    // Fill with constant value
    for (uint32_t x = 0; x < size; ++x) {
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t z = 0; z < size; ++z) {
                if (value != 0.0f) {
                    acc.setValue(nanovdb::Coord(x, y, z), value);
                }
            }
        }
    }

    // Convert to NanoGrid
    auto handle = nanovdb::tools::createNanoGrid(srcGrid);
    return handle;
}

inline nanovdb::GridHandle<nanovdb::HostBuffer>
VulkanFixture::createGradientTestGrid(uint32_t size)
{
    // Create a CPU-side grid using NanoVDB's build tools
    using SrcGridT = nanovdb::tools::build::FloatGrid;

    SrcGridT srcGrid(0.0f);  // background value
    auto acc = srcGrid.getAccessor();

    // Fill with gradient values
    float maxVal = 3.0f * size;
    for (uint32_t x = 0; x < size; ++x) {
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t z = 0; z < size; ++z) {
                float gradValue = (x + y + z) / maxVal;
                if (gradValue != 0.0f) {
                    acc.setValue(nanovdb::Coord(x, y, z), gradValue);
                }
            }
        }
    }

    // Convert to NanoGrid
    auto handle = nanovdb::tools::createNanoGrid(srcGrid);
    return handle;
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
