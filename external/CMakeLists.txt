# External dependencies

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
endif()# NanoVDB and other third-party libraries go here
