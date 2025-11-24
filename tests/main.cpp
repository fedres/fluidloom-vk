#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

// Define Vulkan dynamic dispatcher storage
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
