#pragma once

// Vulkan configuration for consistent Volk usage
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

// Include volk first before any other Vulkan headers
#include <volk.h>

// Then include standard Vulkan headers
#include <vulkan/vulkan.h>

// Tell vulkan.hpp to use dynamic dispatcher with volk
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

// Declare our global dispatcher - defined in VulkanContext.cpp
namespace vk::detail {
    extern DispatchLoaderDynamic defaultDispatchLoaderDynamic;
}