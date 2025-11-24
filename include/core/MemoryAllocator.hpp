#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <cstddef>

namespace core {

class VulkanContext;

/**
 * @brief GPU memory management using Vulkan Memory Allocator (VMA)
 *
 * Provides simplified buffer allocation with support for device address access (bindless).
 * Handles both GPU-local and host-visible memory allocations.
 */
class MemoryAllocator {
public:
    /**
     * @brief Allocated GPU buffer descriptor
     */
    struct Buffer {
        vk::Buffer handle;                       // C++ buffer handle
        VmaAllocation allocation = nullptr;      // VMA allocation handle
        vk::DeviceAddress deviceAddress = {};    // For shader bindless access
        vk::DeviceSize size = 0;
        void* mappedData = nullptr;              // Non-null if persistently mapped
    };

    /**
     * Initialize memory allocator with Vulkan context
     * @param context Initialized VulkanContext
     */
    explicit MemoryAllocator(const VulkanContext& context);

    /**
     * Cleanup and destroy all allocated memory
     */
    ~MemoryAllocator();

    /**
     * Create a GPU buffer with specified usage and memory type
     * @param size Buffer size in bytes
     * @param usage Vulkan buffer usage flags
     * @param memoryUsage VMA memory usage type
     * @return Allocated buffer descriptor
     */
    Buffer createBuffer(vk::DeviceSize size,
                       vk::BufferUsageFlags usage,
                       VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO);

    /**
     * Destroy and deallocate a buffer
     * @param buffer Buffer to destroy
     */
    void destroyBuffer(Buffer& buffer);

    /**
     * Copy data from CPU to GPU using a staging buffer
     * Allocates a temporary staging buffer, copies data, and submits transfer commands
     * @param dst Destination GPU buffer
     * @param srcData Source CPU data pointer
     * @param size Number of bytes to copy
     * @param offset Offset within destination buffer
     */
    void uploadToGPU(const Buffer& dst, const void* srcData,
                    vk::DeviceSize size, vk::DeviceSize offset = 0);

    /**
     * Get device address for bindless access
     * @param buffer Buffer to query
     * @return Device address (64-bit pointer)
     */
    vk::DeviceAddress getBufferAddress(const Buffer& buffer);

private:
    VmaAllocator m_allocator = nullptr;
    const VulkanContext& m_context;

    // Command pool for one-time transfers
    vk::CommandPool m_transferCommandPool;
};

} // namespace core
