#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "core/MemoryAllocator.hpp"
#include "core/VulkanContext.hpp"
#include "core/Logger.hpp"

#include <stdexcept>

namespace core {

MemoryAllocator::MemoryAllocator(const VulkanContext& context)
    : m_context(context) {
    LOG_INFO("Initializing MemoryAllocator...");

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    // Convert C++ handles to C handles for VMA
    allocatorInfo.instance = static_cast<VkInstance>(context.getInstance());
    allocatorInfo.physicalDevice = static_cast<VkPhysicalDevice>(context.getPhysicalDevice());
    allocatorInfo.device = static_cast<VkDevice>(context.getDevice());

    VkResult result = vmaCreateAllocator(&allocatorInfo, &m_allocator);
    LOG_CHECK(result == VK_SUCCESS, "Failed to create VMA allocator");

    // Create command pool for transfers
    m_transferCommandPool = context.createCommandPool(
        context.getQueues().transferFamily,
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    LOG_INFO("MemoryAllocator initialized successfully");
}

MemoryAllocator::~MemoryAllocator() {
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = nullptr;
    }

    if (m_transferCommandPool) {
        m_context.getDevice().destroyCommandPool(m_transferCommandPool);
    }

    LOG_DEBUG("MemoryAllocator destroyed");
}

MemoryAllocator::Buffer MemoryAllocator::createBuffer(vk::DeviceSize size,
                                                       vk::BufferUsageFlags usage,
                                                       VmaMemoryUsage memoryUsage) {
    // Create buffer info using C++ API
    vk::BufferCreateInfo bufferInfo{
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };

    // Convert to C structure for VMA
    VkBufferCreateInfo vkBufferInfo = bufferInfo;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    // For host-visible allocations, set appropriate flags
    if (memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY ||
        memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU ||
        memoryUsage == VMA_MEMORY_USAGE_GPU_TO_CPU) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    Buffer buffer = {};
    buffer.size = size;

    VkBuffer rawBuffer;
    VkResult result = vmaCreateBuffer(m_allocator, &vkBufferInfo, &allocInfo,
                                     &rawBuffer, &buffer.allocation, nullptr);

    LOG_CHECK(result == VK_SUCCESS, "Failed to allocate buffer");

    buffer.handle = vk::Buffer(rawBuffer);

    // Get device address for bindless access
    vk::BufferDeviceAddressInfo addressInfo{
        .buffer = buffer.handle
    };

    buffer.deviceAddress = m_context.getDevice().getBufferAddress(addressInfo);

    // Map memory if allocated in host-visible memory
    VmaAllocationInfo vmaAllocInfo;
    vmaGetAllocationInfo(m_allocator, buffer.allocation, &vmaAllocInfo);
    if (vmaAllocInfo.pMappedData) {
        buffer.mappedData = vmaAllocInfo.pMappedData;
    }

    LOG_DEBUG("Allocated buffer of size {} bytes (address: 0x{:x})",
              size, static_cast<uint64_t>(buffer.deviceAddress));

    return buffer;
}

void MemoryAllocator::destroyBuffer(Buffer& buffer) {
    if (buffer.handle && buffer.allocation) {
        vmaDestroyBuffer(m_allocator, static_cast<VkBuffer>(buffer.handle), buffer.allocation);
        buffer.handle = nullptr;
        buffer.allocation = nullptr;
        buffer.deviceAddress = {};
        buffer.mappedData = nullptr;
    }
}

void MemoryAllocator::uploadToGPU(const Buffer& dst, const void* srcData,
                                   vk::DeviceSize size, vk::DeviceSize offset) {
    if (!srcData || size == 0) {
        LOG_WARN("uploadToGPU called with null data or zero size");
        return;
    }

    LOG_DEBUG("Uploading {} bytes to GPU at offset {}", size, offset);

    // Create staging buffer
    Buffer staging = createBuffer(
        size,
        vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_CPU_ONLY);

    // Copy data to staging
    if (staging.mappedData) {
        memcpy(staging.mappedData, srcData, size);

        // Flush if needed
        vmaFlushAllocation(m_allocator, staging.allocation, 0, size);
    } else {
        LOG_ERROR("Staging buffer not host-visible");
        destroyBuffer(staging);
        throw std::runtime_error("Staging buffer not host-accessible");
    }

    // Submit transfer command
    try {
        vk::CommandPool cmdPool = m_transferCommandPool;
        vk::CommandBuffer cmd = m_context.beginSingleTimeCommands(cmdPool);

        vk::BufferCopy copyRegion{
            .srcOffset = 0,
            .dstOffset = offset,
            .size = size
        };

        cmd.copyBuffer(staging.handle, dst.handle, copyRegion);

        m_context.endSingleTimeCommands(cmd, cmdPool, m_context.getQueues().transfer);

        LOG_DEBUG("GPU upload complete");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to upload data to GPU: {}", e.what());
        destroyBuffer(staging);
        throw;
    }

    destroyBuffer(staging);
}

vk::DeviceAddress MemoryAllocator::getBufferAddress(const Buffer& buffer) {
    vk::BufferDeviceAddressInfo addressInfo{
        .buffer = buffer.handle
    };

    return m_context.getDevice().getBufferAddress(addressInfo);
}

} // namespace core
