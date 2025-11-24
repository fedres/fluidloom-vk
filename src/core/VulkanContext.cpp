#include "core/VulkanConfig.hpp"
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

    // On all platforms, volk should handle loading automatically
    // For macOS with MoltenVK, ensure ICD path is set BEFORE volk initialization if needed
    #ifdef __APPLE__
    // Check if ICD path is already set
    const char* existing_icd = getenv("VK_ICD_FILENAMES");
    if (!existing_icd) {
        const char* icdPath = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
        setenv("VK_ICD_FILENAMES", icdPath, 1);  // Overwrite to ensure it's set
        LOG_INFO("Set VK_ICD_FILENAMES to: {}", icdPath);
    } else {
        LOG_INFO("VK_ICD_FILENAMES already set to: {}", existing_icd);
    }
    #endif

    VkResult volkResult = volkInitialize();
    if (volkResult != VK_SUCCESS) {
        LOG_ERROR("Failed to initialize volk. Error code: {}", static_cast<int>(volkResult));
        LOG_ERROR("Make sure Vulkan SDK is installed and environment variables are configured");
        throw std::runtime_error("Failed to initialize volk. Error code: " + std::to_string(static_cast<int>(volkResult)));
    }
    LOG_INFO("Volk initialized successfully");

    // Step 2: Initialize Vulkan-HPP default dispatcher with vkGetInstanceProcAddr
    // Note: dispatcher will be initialized when instance is created
    LOG_INFO("Vulkan-HPP dispatcher will be initialized after instance creation");

    try {
        // Step 3: Create Vulkan instance using vk-bootstrap
        LOG_INFO("Creating Vulkan instance...");
        auto instanceBuilder = vkb::InstanceBuilder()
            .set_app_name("FluidEngine")
            .set_app_version(0, 1, 0)
            .set_engine_name("FluidEngine")
            .set_engine_version(0, 1, 0);

        // Try Vulkan 1.3 first, fallback to 1.2 if needed (common on macOS/MoltenVK)
        bool useVulkan13 = true;
        #ifdef __APPLE__
        // On macOS with MoltenVK, start with 1.2 as it's more reliably supported
        useVulkan13 = false;
        instanceBuilder.require_api_version(1, 2, 0);
        #else
        instanceBuilder.require_api_version(1, 3, 0);
        #endif

        #ifdef __APPLE__
        // On macOS with MoltenVK, we need to enable portability enumeration
        instanceBuilder.enable_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        // Note: In this version of vk-bootstrap, flags are set via InstanceInfo
        LOG_INFO("Enabled portability enumeration for macOS/MoltenVK");
        #endif

        if (enableValidation) {
            instanceBuilder
                .request_validation_layers()
                .use_default_debug_messenger();
            LOG_INFO("Validation layers enabled");
        }

        auto instanceResult = instanceBuilder.build();

        // If Vulkan 1.3 failed and we haven't tried 1.2 yet, try fallback
        if (!instanceResult && useVulkan13) {
            LOG_WARN("Vulkan 1.3 instance creation failed: {}", instanceResult.error().message());
            LOG_WARN("Trying Vulkan 1.2 fallback...");
            instanceBuilder.require_api_version(1, 2, 0);
            instanceResult = instanceBuilder.build();
        }

        if (!instanceResult.has_value()) {
            LOG_ERROR("Failed to create Vulkan instance: {}", instanceResult.error().message());
            throw std::runtime_error("Failed to create Vulkan instance: " + instanceResult.error().message());
        }
        m_vkbInstance = instanceResult.value();

        // Wrap vk-bootstrap instance in C++ type
        m_instance = vk::Instance(m_vkbInstance.instance);
        LOG_INFO("Vulkan instance created successfully");

        // Load instance-level functions
        volkLoadInstance(m_vkbInstance.instance);

        // Initialize instance-level dispatcher
        vk::detail::defaultDispatchLoaderDynamic.init(m_instance);

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
                 std::string(props.deviceName.data()),
                 vk::to_string(props.deviceType));

        // Step 5: Create logical device with required features
        LOG_INFO("Creating logical device...");

        // Vulkan 1.2 features
        vk::PhysicalDeviceVulkan12Features features12;
        features12.setBufferDeviceAddress(VK_TRUE);
        features12.setDescriptorIndexing(VK_TRUE);
        features12.setShaderStorageBufferArrayNonUniformIndexing(VK_TRUE);
        features12.setRuntimeDescriptorArray(VK_TRUE);
        features12.setDescriptorBindingVariableDescriptorCount(VK_TRUE);

        // Vulkan 1.3 features
        vk::PhysicalDeviceVulkan13Features features13;
        features13.setSynchronization2(VK_TRUE);
        features13.setDynamicRendering(VK_TRUE);

        features12.setPNext(&features13);

        vk::PhysicalDeviceFeatures2 features2;
        features2.setPNext(&features12);
        features2.features.setShaderInt64(VK_TRUE);
        features2.features.setFragmentStoresAndAtomics(VK_TRUE);

        // Queue info
        // Find compute queue family
        auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();
        uint32_t computeFamily = -1;
        for (uint32_t i = 0; i < queueFamilies.size(); i++) {
            if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) {
                computeFamily = i;
                break;
            }
        }
        
        if (computeFamily == -1) {
            throw std::runtime_error("Failed to find compute queue family");
        }

        m_queues.computeFamily = computeFamily;
        m_queues.transferFamily = computeFamily; // Use same for simplicity

        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.setQueueFamilyIndex(computeFamily);
        queueCreateInfo.setQueueCount(1);
        queueCreateInfo.setPQueuePriorities(&queuePriority);

        // Extensions - only add what's actually needed for compute
        std::vector<const char*> extensions;
        
        #ifdef __APPLE__
        // On macOS with MoltenVK, we need portability subset
        extensions.push_back("VK_KHR_portability_subset");
        LOG_INFO("Added VK_KHR_portability_subset for macOS/MoltenVK");
        #endif
        
        // Only add swapchain if we're doing graphics (not for compute-only tests)
        // Note: Commenting out swapchain for compute-only tests
        // extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        vk::DeviceCreateInfo createInfo;
        createInfo.setPNext(&features2);
        createInfo.setQueueCreateInfoCount(1);
        createInfo.setPQueueCreateInfos(&queueCreateInfo);
        createInfo.setEnabledExtensionCount(static_cast<uint32_t>(extensions.size()));
        createInfo.setPpEnabledExtensionNames(extensions.data());

        // Try to create device with better error handling
        try {
            m_device = m_physicalDevice.createDevice(createInfo);
        } catch (const vk::SystemError& e) {
            LOG_ERROR("Vulkan device creation failed: {}", e.what());
            
            // Try with minimal features as fallback
            LOG_WARN("Attempting device creation with minimal features...");
            vk::PhysicalDeviceFeatures2 minimalFeatures2{};
            vk::PhysicalDeviceVulkan12Features minimalFeatures12{};
            minimalFeatures12.setBufferDeviceAddress(VK_TRUE);
            minimalFeatures12.setPNext(nullptr);
            minimalFeatures2.setPNext(&minimalFeatures12);
            
            createInfo.setPNext(&minimalFeatures2);
            m_device = m_physicalDevice.createDevice(createInfo);
        }
        
        // Initialize volk for device
        volkLoadDevice(m_device);

        // Get queue
        m_queues.compute = m_device.getQueue(computeFamily, 0);
        m_queues.transfer = m_queues.compute;
        
        m_initialized = true;
        LOG_INFO("Logical device created successfully");

    } catch (const vk::SystemError& e) {
        LOG_ERROR("Vulkan system error during device creation: {} (code: {})",
                  e.what(), static_cast<int>(e.code().value()));
        throw;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create logical device: {}", e.what());
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
        try {
            m_device.waitIdle();
            m_device.destroy();
            LOG_INFO("Vulkan device destroyed successfully");
        } catch (const std::exception& e) {
            LOG_WARN("Error destroying Vulkan device: {}", e.what());
        }
        m_device = nullptr;
    }

    // Destroy instance and debug messenger
    if (m_instance) {
        try {
            m_instance.destroy();
            LOG_INFO("Vulkan instance destroyed successfully");
        } catch (const std::exception& e) {
            LOG_WARN("Error destroying Vulkan instance: {}", e.what());
        }
        m_instance = nullptr;
    }

    m_initialized = false;
    LOG_INFO("VulkanContext cleanup complete");
}

vk::CommandPool VulkanContext::createCommandPool(uint32_t queueFamily, vk::CommandPoolCreateFlags flags) const {
    if (!m_initialized) {
        throw std::runtime_error("VulkanContext not initialized");
    }

    vk::CommandPoolCreateInfo createInfo;
    createInfo.setFlags(flags);
    createInfo.setQueueFamilyIndex(queueFamily);

    return m_device.createCommandPool(createInfo);
}

vk::CommandBuffer VulkanContext::beginSingleTimeCommands(vk::CommandPool pool) const {
    if (!m_initialized) {
        throw std::runtime_error("VulkanContext not initialized");
    }

    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.setCommandPool(pool);
    allocInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    allocInfo.setCommandBufferCount(1);

    vk::CommandBuffer cmd;
    auto result = m_device.allocateCommandBuffers(&allocInfo, &cmd);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to allocate command buffer");
    }

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

    cmd.begin(beginInfo);
    return cmd;
}

void VulkanContext::endSingleTimeCommands(vk::CommandBuffer cmd,
                                          vk::CommandPool pool,
                                          vk::Queue queue) const {
    cmd.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBufferCount(1);
    submitInfo.setPCommandBuffers(&cmd);

    queue.submit(submitInfo, nullptr);
    queue.waitIdle();

    m_device.freeCommandBuffers(pool, 1, &cmd);
}

bool VulkanContext::isFeatureSupported(const std::string& featureName) const {
    if (!m_initialized) {
        return false;
    }

    if (featureName == "bufferDeviceAddress") return true;
    if (featureName == "descriptorIndexing") return true;
    if (featureName == "timelineSemaphore") return true;
    if (featureName == "synchronization2") return true;
    
    if (featureName == "shaderInt64") {
        vk::PhysicalDeviceFeatures features = m_physicalDevice.getFeatures();
        return features.shaderInt64;
    }
    if (featureName == "shaderFloat64") {
        vk::PhysicalDeviceFeatures features = m_physicalDevice.getFeatures();
        return features.shaderFloat64;
    }

    LOG_WARN("Unknown feature check: {}", featureName);
    return false;
}

} // namespace core
