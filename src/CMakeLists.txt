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

    # Halo exchange system
    halo/HaloManager.cpp
    halo/HaloSync.cpp

    # Stencil system
    stencil/ShaderGenerator.cpp
    stencil/StencilRegistry.cpp
    stencil/StencilParser.cpp
    stencil/PipelineCache.cpp

    # Execution graph (DAG scheduler)
    graph/DependencyGraph.cpp
    graph/GraphExecutor.cpp

    # Lua scripting
    script/LuaContext.cpp
    script/SimulationEngine.cpp

    # Refinement system (dynamic topology adaptation) - TEMPORARILY DISABLED
    # refinement/RefinementManager.cpp
    # refinement/TopologyRebuilder.cpp

    # Visualization system (volume rendering) - TEMPORARILY DISABLED
    # vis/VolumeRenderer.cpp
)

# Define VK_NO_PROTOTYPES globally to prevent conflicts between volk and vulkan.h
add_compile_definitions(VK_NO_PROTOTYPES)

# Create static library
add_library(fluidloom STATIC ${FLUIDLOOM_SOURCES})

# Link dependencies
target_link_libraries(fluidloom
    PUBLIC
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
)

# Link volk if found
if(volk_FOUND)
    target_link_libraries(fluidloom PUBLIC volk::volk)
endif()

# Link optional dependencies if found
if(VulkanMemoryAllocator_FOUND)
    target_link_libraries(fluidloom PUBLIC GPUOpen::VulkanMemoryAllocator)
    # VMA is header-only, ensure include path is set
    target_include_directories(fluidloom PUBLIC ${VulkanMemoryAllocator_INCLUDE_DIRS})
endif()

# Link external spdlog (header-only)
if(TARGET spdlog)
    target_link_libraries(fluidloom PUBLIC spdlog::spdlog)
endif()

if(nanovdb_FOUND)
    target_link_libraries(fluidloom PUBLIC nanovdb::nanovdb)
endif()

if(Lua_FOUND)
    target_link_libraries(fluidloom PUBLIC ${LUA_LIBRARIES})
    target_include_directories(fluidloom PUBLIC ${LUA_INCLUDE_DIR})
endif()

if(sol2_FOUND)
    target_link_libraries(fluidloom PUBLIC sol2::sol2)
endif()

if(glm_FOUND)
    target_link_libraries(fluidloom PUBLIC glm::glm)
endif()

if(OpenSSL_FOUND)
    target_link_libraries(fluidloom PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Set include directories
target_include_directories(fluidloom
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../external
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/spdlog/include  # External spdlog first
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/openvdb/nanovdb
)


