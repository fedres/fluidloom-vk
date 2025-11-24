# Source files (.cpp)
set(FLUIDLOOM_SOURCES
    # Core infrastructure
    core/Logger.cpp
    core/VulkanContext.cpp
    core/MemoryAllocator.cpp

    # NanoVDB integration
    nanovdb_adapter/GridLoader.cpp
    nanovdb_adapter/GpuGridManager.cpp

    # Domain decomposition
    domain/DomainSplitter.cpp

    # Field system
    field/FieldRegistry.cpp
)

# Create static library
add_library(fluidloom STATIC ${FLUIDLOOM_SOURCES})

# Link dependencies
target_link_libraries(fluidloom
    PUBLIC
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        nanovdb::nanovdb
        spdlog::spdlog
)

# Set include directories
target_include_directories(fluidloom
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
