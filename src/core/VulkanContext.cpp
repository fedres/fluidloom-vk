#include "core/VulkanContext.hpp"
#include "core/Logger.hpp"

#include <volk.h>
#include <vulkan/vulkan.hpp>
#include <stdexcept>

namespace core {

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    cleanup();
}

void VulkanContext::init(bool enableValidation) {
    if (m_initialized) {
        LOG_WARN("VulkanContext already initialized");
        return;
    }

    // Step 1: Initialize volk (meta-loader)
    LOG_INFO("Initializing volk (Vulkan meta-loader)...");
    VkResult volkResult = volkInitialize();
    LOG_CHECK(volkResult == VK_SUCCESS, "Failed to initialize volk");

    // Step 2: Initialize Vulkan-HPP default dispatcher with vkGetInstanceProcAddr
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
    LOG_INFO("Vulkan-HPP dispatcher initialized");

    try {
        // Step 3: Create Vulkan instance using vk-bootstrap
        LOG_INFO("Creating Vulkan instance...");
        auto instanceBuilder = vkb::InstanceBuilder()
            .set_app_name("FluidEngine")
            .set_app_version(0, 1, 0)
            .set_engine_name("FluidEngine")
            .set_engine_version(0, 1, 0)
            .require_api_version(1, 3, 0);

        if (enableValidation) {
            instanceBuilder
                .request_validation_layers()
                .use_default_debug_messenger();
            LOG_INFO("Validation layers enabled");
        }

        auto instanceResult = instanceBuilder.build();
        LOG_CHECK(instanceResult.has_value(), "Failed to create Vulkan instance");
        m_vkbInstance = instanceResult.value();

        // Wrap vk-bootstrap instance in C++ type
        m_instance = vk::Instance(m_vkbInstance.instance);
        LOG_INFO("Vulkan instance created successfully");

        // Load instance-level functions
        volkLoadInstance(m_vkbInstance.instance);

        // Initialize instance-level dispatcher
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

        // Step 4: Select physical device
        LOG_INFO("Selecting physical device...");
        auto physicalDeviceSelector = vkb::PhysicalDeviceSelector(m_vkbInstance)
            .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
            .allow_any_gpu_device_type(true)
            .require_present(false); // Compute-only, no presentation

        auto physicalDeviceResult = physicalDeviceSelector.select();
        LOG_CHECK(physicalDeviceResult.has_value(), "Failed to select physical device");
        m_vkbPhysicalDevice = physicalDeviceResult.value();

        // Wrap in C++ type
        m_physicalDevice = vk::PhysicalDevice(m_vkbPhysicalDevice.physical_device);

        vk::PhysicalDeviceProperties props = m_physicalDevice.getProperties();
        LOG_INFO("Selected physical device: {} (type: {})",
                 props.deviceName,
                 vk::to_string(props.deviceType));

        // Step 5: Create logical device with required features
        LOG_INFO("Creating logical device...");

        // Vulkan 1.2 features
        vk::PhysicalDeviceVulkan12Features features12{
            .bufferDeviceAddress = VK_TRUE,
            .timelineSemaphore = VK_TRUE,
            .descriptorIndexing = VK_TRUE,
            .runtimeDescriptorArray = VK_TRUE,
            .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE
        };

        // Vulkan 1.3 features
        vk::PhysicalDeviceVulkan13Features features13{
            .synchronization2 = VK_TRUE,
            .pNext = &features12
        };

        auto deviceBuilder = vkb::DeviceBuilder(m_vkbPhysicalDevice)
            .add_pNext(&features13);

        auto deviceResult = deviceBuilder.build();
        LOG_CHECK(deviceResult.has_value(), "Failed to create logical device");
        m_vkbDevice = deviceResult.value();

        // Wrap in C++ type
        m_device = vk::Device(m_vkbDevice.device);
        LOG_INFO("Logical device created successfully");

        // Load device-level functions
        volkLoadDevice(m_vkbDevice.device);

        // Initialize device-level dispatcher
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

        // Step 6: Get queues
        LOG_INFO("Getting queue handles...");
        auto computeQueueResult = m_vkbDevice.get_queue(vkb::QueueType::compute);
        LOG_CHECK(computeQueueResult.has_value(), "Failed to get compute queue");
        m_queues.compute = vk::Queue(computeQueueResult.value());
        m_queues.computeFamily = m_vkbDevice.get_queue_index(vkb::QueueType::compute).value();

        auto transferQueueResult = m_vkbDevice.get_queue(vkb::QueueType::transfer);
        if (transferQueueResult.has_value()) {
            m_queues.transfer = vk::Queue(transferQueueResult.value());
            m_queues.transferFamily = m_vkbDevice.get_queue_index(vkb::QueueType::transfer).value();
        } else {
            // Fallback to compute queue for transfers
            m_queues.transfer = m_queues.compute;
            m_queues.transferFamily = m_queues.computeFamily;
            LOG_WARN("Using compute queue for transfers");
        }

        LOG_INFO("Queues acquired: compute family {}, transfer family {}",
                 m_queues.computeFamily, m_queues.transferFamily);

        m_initialized = true;
        LOG_INFO("VulkanContext initialization complete");

    } catch (const std::exception& e) {
        LOG_ERROR("Exception during Vulkan initialization: {}", e.what());
        cleanup();
        throw;
    }
}

void VulkanContext::cleanup() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Cleaning up VulkanContext...");

    // Device must be cleaned up before instance
    if (m_device) {
        m_device.waitIdle();
        m_device = nullptr;
    }

    // Destroy instance and debug messenger
    if (m_instance) {
        m_instance = nullptr;
    }

    m_initialized = false;
    LOG_INFO("VulkanContext cleanup complete");
}

vk::CommandPool VulkanContext::createCommandPool(uint32_t queueFamily,
                                                  vk::CommandPoolCreateFlags flags) {
    if (!m_initialized) {
        throw std::runtime_error("VulkanContext not initialized");
    }

    vk::CommandPoolCreateInfo createInfo{
        .flags = flags,
        .queueFamilyIndex = queueFamily
    };

    return m_device.createCommandPool(createInfo);
}

vk::CommandBuffer VulkanContext::beginSingleTimeCommands(vk::CommandPool pool) {
    if (!m_initialized) {
        throw std::runtime_error("VulkanContext not initialized");
    }

    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    vk::CommandBuffer cmd = m_device.allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };

    cmd.begin(beginInfo);
    return cmd;
}

void VulkanContext::endSingleTimeCommands(vk::CommandBuffer cmd,
                                          vk::CommandPool pool,
                                          vk::Queue queue) {
    if (!m_initialized) {
        throw std::runtime_error("VulkanContext not initialized");
    }

    cmd.end();

    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };

    queue.submit(submitInfo);
    queue.waitIdle();

    m_device.freeCommandBuffers(pool, cmd);
}

bool VulkanContext::isFeatureSupported(const std::string& featureName) const {
    if (!m_initialized) {
        return false;
    }

    // This is a placeholder for checking specific features
    // Can be extended to check specific extensions or features
    LOG_DEBUG("Checking feature support: {}", featureName);
    return true;
}

} // namespace core
