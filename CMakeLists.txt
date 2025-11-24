cmake_minimum_required(VERSION 3.28)
project(FluidLoom)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable find_package to look in standard locations
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find required packages
find_package(Vulkan REQUIRED)
find_package(vk-bootstrap REQUIRED)
find_package(volk REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(nanovdb REQUIRED)
find_package(spdlog REQUIRED)
find_package(sol2 REQUIRED)    # Lua bindings
find_package(glm REQUIRED)     # Math library
find_package(Catch2 QUIET)     # Testing framework (optional)

# Include directories for the project
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add external dependencies
add_subdirectory(external)

# Add source directories
add_subdirectory(src)

# Add tests if Catch2 is available
if(Catch2_FOUND)
    add_subdirectory(tests)
else()
    message(STATUS "Catch2 not found - tests will be skipped")
endif()
