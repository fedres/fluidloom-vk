#pragma once

#include <vulkan/vulkan.hpp>
#include <vk-bootstrap/VkBootstrap.h>
#include <cstdint>
#include <memory>

namespace core {

/**
 * @brief Manages Vulkan instance, physical device, and logical device initialization
 *
 * Uses vk-bootstrap for simplified setup and vulkan.hpp for C++ bindings.
 * Provides thread-safe access to Vulkan objects and command pool creation.
 */
class VulkanContext {
public:
    /**
     * @brief Queue information holder
     */
    struct Queues {
        vk::Queue compute;
        uint32_t computeFamily = 0;
        vk::Queue transfer;
        uint32_t transferFamily = 0;
    };

    VulkanContext();
    ~VulkanContext();

    /**
     * Initialize Vulkan 1.3 with required features and extensions
     * @param enableValidation Enable Vulkan validation layers (recommended for debugging)
     */
    void init(bool enableValidation = true);

    /**
     * Cleanup all Vulkan resources
     */
    void cleanup();

    /**
     * Get the Vulkan instance (vk::Instance using C++ bindings)
     */
    vk::Instance getInstance() const { return m_instance; }

    /**
     * Get the physical device (vk::PhysicalDevice)
     */
    vk::PhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }

    /**
     * Get the logical device (vk::Device)
     */
    vk::Device getDevice() const { return m_device; }

    /**
     * Get queue information (compute and transfer queues)
     */
    const Queues& getQueues() const { return m_queues; }

    /**
     * Create a command pool for recording commands
     * @param queueFamily Queue family index
     * @param flags Optional command pool creation flags
     */
    vk::CommandPool createCommandPool(uint32_t queueFamily,
                                      vk::CommandPoolCreateFlags flags = {});

    /**
     * Helper to begin recording a single-time-use command buffer
     * @param pool Command pool to allocate from
     * @return Command buffer ready for recording
     */
    vk::CommandBuffer beginSingleTimeCommands(vk::CommandPool pool);

    /**
     * Helper to end, submit, and wait for a single-time command buffer
     * @param cmd Command buffer to submit
     * @param pool Command pool that allocated the buffer
     * @param queue Queue to submit to
     */
    void endSingleTimeCommands(vk::CommandBuffer cmd, vk::CommandPool pool, vk::Queue queue);

    /**
     * Check if a specific feature is supported
     */
    bool isFeatureSupported(const std::string& featureName) const;

private:
    vk::Instance m_instance;
    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;
    Queues m_queues;

    // Keep vk-bootstrap objects for cleanup and feature queries
    vkb::Instance m_vkbInstance;
    vkb::PhysicalDevice m_vkbPhysicalDevice;
    vkb::Device m_vkbDevice;

    bool m_initialized = false;
};

} // namespace core
