cmake_minimum_required(VERSION 3.20)
project(FluidLoom)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add Homebrew paths for M1/M2 Macs
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

# Enable find_package to look in standard locations
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find Vulkan (REQUIRED - installed via Homebrew)
find_package(Vulkan REQUIRED COMPONENTS glslc)
message(STATUS "Found Vulkan: ${Vulkan_LIBRARY}")
message(STATUS "Found glslc: ${Vulkan_GLSLC_EXECUTABLE}")

# volk is installed via Homebrew
find_package(volk CONFIG QUIET)
if(volk_FOUND)
    message(STATUS "Found volk")
else()
    message(WARNING "volk not found - will use direct Vulkan calls")
endif()

# Optional dependencies - gracefully handle missing ones
find_package(VulkanMemoryAllocator CONFIG QUIET)
find_package(nanovdb CONFIG QUIET)
# spdlog is built from external/spdlog
find_package(Lua QUIET)
find_package(sol2 CONFIG QUIET)
find_package(glm CONFIG QUIET)
find_package(OpenSSL QUIET)
find_package(Catch2 3 QUIET)

# Add external dependencies if they exist
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CMakeLists.txt")
    add_subdirectory(external)
endif()

# Include directories for the project
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories("/opt/homebrew/include")

# Add source directories
add_subdirectory(src)

# Add tools
add_subdirectory(tools)

# Main executable
add_executable(fluidloom_app src/main.cpp)
target_link_libraries(fluidloom_app PRIVATE fluidloom)
target_include_directories(fluidloom_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add tests if Catch2 is available
if(Catch2_FOUND)
    message(STATUS "Catch2 found - enabling tests")
    enable_testing()
    add_subdirectory(tests)
else()
    message(STATUS "Catch2 not found - tests will be skipped")
endif()
