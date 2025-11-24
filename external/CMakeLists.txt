# External dependencies
include(FetchContent)

# volk - Vulkan meta-loader
# Try to find it first (for local installations), then fetch if not found
find_package(volk CONFIG QUIET)
if(NOT volk_FOUND)
    message(STATUS "volk not found locally, fetching from GitHub...")
    FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.3.275.0
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/volk
    )
    FetchContent_MakeAvailable(volk)
    message(STATUS "volk fetched and configured")
endif()

# vk-bootstrap for Vulkan initialization
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vk-bootstrap/CMakeLists.txt")
    add_subdirectory(vk-bootstrap)
endif()

# spdlog for logging (compiled library mode)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/spdlog/CMakeLists.txt")
    # Configure spdlog to be a compiled library
    set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(spdlog)
endif()

# NanoVDB and other third-party libraries go here
