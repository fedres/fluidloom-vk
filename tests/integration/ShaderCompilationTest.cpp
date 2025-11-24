#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

// Define Vulkan dynamic dispatcher storage
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "core/VulkanConfig.hpp"
#include "stencil/StencilRegistry.hpp"
#include "field/FieldRegistry.hpp"
#include "core/VulkanContext.hpp"
#include "core/MemoryAllocator.hpp"
#include "core/Logger.hpp"
#include <iostream>
#include <vector>

int main() {
    core::Logger::init();
    LOG_INFO("Starting Shader Compilation Test");

    try {
        // On macOS, ensure Vulkan can find MoltenVK ICD
        #ifdef __APPLE__
        const char* icdPath = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
        setenv("VK_ICD_FILENAMES", icdPath, 0);  // Don't overwrite if already set
        setenv("VK_DRIVER_FILES", icdPath, 0);
        LOG_INFO("Set VK_ICD_FILENAMES to: {}", icdPath);
        #endif

        // Initialize Vulkan Context (headless)
        core::VulkanContext context;
        context.init(false);

        core::MemoryAllocator allocator(context);
        field::FieldRegistry fieldRegistry(context, allocator, 1024);
        stencil::StencilRegistry stencilRegistry(context, fieldRegistry);

        // Test GLSL source
        std::string glslSource = R"(
            #version 460
            layout(local_size_x = 1) in;
            void main() {
                // Simple compute shader
            }
        )";

        LOG_INFO("Compiling test shader...");
        std::vector<uint32_t> spirv = stencilRegistry.compileToSPIRV(glslSource);

        if (spirv.empty()) {
            LOG_ERROR("Compilation returned empty SPIR-V");
            return 1;
        }

        LOG_INFO("Compilation successful. SPIR-V size: {} bytes", spirv.size() * 4);

        // Verify SPIR-V header magic number
        if (spirv[0] != 0x07230203) {
            LOG_ERROR("Invalid SPIR-V magic number: 0x{:x}", spirv[0]);
            return 1;
        }

        LOG_INFO("Test Passed");
        return 0;

    } catch (const std::exception& e) {
        LOG_ERROR("Test Failed: {}", e.what());
        return 1;
    }
}
