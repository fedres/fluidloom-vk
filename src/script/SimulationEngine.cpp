#include "script/SimulationEngine.hpp"
#include "nanovdb_adapter/GridLoader.hpp"
#include "core/Logger.hpp"

#include <stdexcept>
#include <algorithm>

namespace script {

SimulationEngine::SimulationEngine(const Config& config)
    : m_config(config) {
    LOG_INFO("Initializing SimulationEngine (GPUs: {}, Halo: {})",
             config.gpuCount, config.haloThickness);

    try {
        initialize();
        m_initialized = true;
        LOG_INFO("SimulationEngine initialized successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize SimulationEngine: {}", e.what());
        throw;
    }
}

SimulationEngine::~SimulationEngine() {
    LOG_DEBUG("SimulationEngine destroyed");
}

void SimulationEngine::initialize() {
    // Initialize Vulkan
    m_vulkanContext = std::make_unique<core::VulkanContext>();
    m_vulkanContext->init(false);  // No validation for production

    // Initialize memory allocator
    m_memoryAllocator = std::make_unique<core::MemoryAllocator>(*m_vulkanContext);

    // Initialize field registry
    uint32_t estimatedVoxels = 1024 * 1024;  // Default estimate
    m_fieldRegistry = std::make_unique<field::FieldRegistry>(
        *m_vulkanContext, *m_memoryAllocator, estimatedVoxels);

    // Initialize stencil registry
    m_stencilRegistry = std::make_unique<stencil::StencilRegistry>(
        *m_vulkanContext, *m_fieldRegistry);

    // Initialize dependency graph
    m_dependencyGraph = std::make_unique<graph::DependencyGraph>();

    // Initialize domain splitter
    domain::DomainSplitter::SplitConfig splitConfig;
    splitConfig.gpuCount = m_config.gpuCount;
    splitConfig.haloThickness = m_config.haloThickness;
    m_domainSplitter = std::make_unique<domain::DomainSplitter>(splitConfig);

    // Initialize GPU grid manager
    m_gridManager = std::make_unique<nanovdb_adapter::GpuGridManager>(
        *m_vulkanContext, *m_memoryAllocator);

    // Initialize graph executor (requires halo manager, which is created later in decomposeDomain)
    // Wait, HaloManager is created in decomposeDomain, but GraphExecutor needs it in constructor.
    // This is a circular dependency or ordering issue.
    // GraphExecutor needs HaloManager reference.
    // We should probably create a dummy or delay GraphExecutor creation until decomposeDomain.
    // Or better, move HaloManager creation to initialize if possible, or make GraphExecutor take it later.
    // But GraphExecutor takes reference.
    
    // Let's defer GraphExecutor creation to decomposeDomain where HaloManager is created.
    
    LOG_DEBUG("All subsystems initialized");
}

void SimulationEngine::loadGrid() {
    LOG_INFO("Loading NanoVDB grid from: {}", m_config.gridFile);

    try {
        // Load grid
        auto hostHandle = nanovdb_adapter::GridLoader::load(m_config.gridFile);

        // Upload to GPU
        m_gridResources = m_gridManager->upload(hostHandle);

        LOG_INFO("Grid loaded: {} active voxels",
                 m_gridResources.activeVoxelCount);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load grid: {}", e.what());
        throw;
    }
}

void SimulationEngine::decomposeDomain() {
    LOG_INFO("Decomposing domain for {} GPUs", m_config.gpuCount);

    if (m_config.gridFile.empty()) {
        LOG_WARN("No grid file specified, skipping domain decomposition");
        return;
    }

    try {
        // Load grid if not already loaded
        if (m_gridResources.activeVoxelCount == 0) {
            loadGrid();
        }

        // Load for decomposition
        auto hostHandle = nanovdb_adapter::GridLoader::load(m_config.gridFile);

        // Decompose
        m_subDomains = m_domainSplitter->split(hostHandle);

        LOG_INFO("Domain decomposed into {} sub-domains", m_subDomains.size());

        // Allocate halos
        m_haloManager = std::make_unique<halo::HaloManager>(
            *m_vulkanContext, *m_memoryAllocator, m_subDomains);

        // For each field, allocate halos
        for (const auto& [fieldName, fieldDesc] : m_fieldRegistry->getFields()) {
            for (uint32_t gpu = 0; gpu < m_subDomains.size(); gpu++) {
                m_haloManager->allocateFieldHalos(fieldName, fieldDesc, gpu);
            }
        }

        // Create timeline semaphores
        m_haloManager->createHaloSemaphores();

        // Create GraphExecutor now that HaloManager exists
        m_graphExecutor = std::make_unique<graph::GraphExecutor>(
            *m_vulkanContext,
            *m_haloManager,
            *m_fieldRegistry
        );

        LOG_DEBUG("Halos allocated for all fields and domains");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to decompose domain: {}", e.what());
        throw;
    }
}

vk::Format SimulationEngine::parseFormat(const std::string& formatStr) {
    // Map format strings to Vulkan formats
    if (formatStr == "R32F") {
        return vk::Format::eR32Sfloat;
    } else if (formatStr == "R32I") {
        return vk::Format::eR32Sint;
    } else if (formatStr == "R32G32F") {
        return vk::Format::eR32G32Sfloat;
    } else if (formatStr == "R32G32I") {
        return vk::Format::eR32G32Sint;
    } else if (formatStr == "R32G32B32F") {
        return vk::Format::eR32G32B32Sfloat;
    } else if (formatStr == "R32G32B32I") {
        return vk::Format::eR32G32B32Sint;
    } else if (formatStr == "R32G32B32A32F") {
        return vk::Format::eR32G32B32A32Sfloat;
    } else if (formatStr == "R32G32B32A32I") {
        return vk::Format::eR32G32B32A32Sint;
    } else {
        LOG_WARN("Unknown format: {}, defaulting to R32F", formatStr);
        return vk::Format::eR32Sfloat;
    }
}

void SimulationEngine::addField(const std::string& name,
                               const std::string& format,
                               const std::string& initialValue) {
    LOG_INFO("Adding field: '{}' (format: {})", name, format);

    try {
        vk::Format vkFormat = parseFormat(format);
        m_fieldRegistry->registerField(name, vkFormat, nullptr);

        LOG_DEBUG("Field '{}' registered", name);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to add field: {}", e.what());
        throw;
    }
}

void SimulationEngine::addStencil(const stencil::StencilDefinition& definition) {
    LOG_INFO("Adding stencil: '{}'", definition.name);

    try {
        // Register in stencil registry
        m_stencilRegistry->registerStencil(definition);

        // Add to dependency graph
        m_dependencyGraph->addNode(definition.name,
                                  definition.inputs,
                                  definition.outputs);

        LOG_DEBUG("Stencil '{}' added and registered", definition.name);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to add stencil: {}", e.what());
        throw;
    }
}

void SimulationEngine::step(float dt) {
    LOG_DEBUG("Executing simulation timestep (dt={}s)", dt);

    if (!m_initialized) {
        throw std::runtime_error("Engine not initialized");
    }

    if (m_subDomains.empty()) {
        LOG_WARN("No domains initialized, skipping timestep");
        return;
    }

    try {
        // Build execution schedule
        auto schedule = m_dependencyGraph->buildSchedule();

        LOG_DEBUG("Execution schedule: {} stencils", schedule.size());

        // For each domain, record and execute timestep
        for (const auto& domain : m_subDomains) {
            LOG_DEBUG("Executing domain {} ({} voxels)",
                     domain.gpuIndex, domain.activeVoxelCount);

            // Create command buffer
            vk::CommandPool cmdPool = m_vulkanContext->createCommandPool(
                m_vulkanContext->getQueues().computeFamily);

            vk::CommandBufferAllocateInfo allocInfo(
                cmdPool,
                vk::CommandBufferLevel::ePrimary,
                1
            );
            vk::CommandBuffer cmd = m_vulkanContext->getDevice().allocateCommandBuffers(allocInfo)[0];

            // Record commands
            if (m_graphExecutor) {
                m_graphExecutor->recordTimestep(cmd, schedule, *m_stencilRegistry, domain, dt);
            } else {
                LOG_WARN("GraphExecutor not initialized (did you call decomposeDomain?)");
            }

            LOG_DEBUG("  Recorded commands for domain {}", domain.gpuIndex);
            
            // Submit to compute queue
            // Get semaphores from GraphExecutor
            const auto& waitSemaphores = m_graphExecutor->getWaitSemaphores();
            const auto& waitValues = m_graphExecutor->getWaitValues();
            const auto& signalSemaphores = m_graphExecutor->getSignalSemaphores();
            const auto& signalValues = m_graphExecutor->getSignalValues();

            vk::TimelineSemaphoreSubmitInfo timelineInfo{};
            timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
            timelineInfo.pWaitSemaphoreValues = waitValues.data();
            timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
            timelineInfo.pSignalSemaphoreValues = signalValues.data();

            vk::SubmitInfo submitInfo{};
            submitInfo.pNext = &timelineInfo;
            
            submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
            submitInfo.pWaitSemaphores = waitSemaphores.data();
            
            std::vector<vk::PipelineStageFlags> waitStages(waitSemaphores.size(), vk::PipelineStageFlagBits::eComputeShader);
            submitInfo.pWaitDstStageMask = waitStages.data();
            
            submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
            submitInfo.pSignalSemaphores = signalSemaphores.data();
            
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;

            // In a real system, we would handle semaphores here from GraphExecutor
            // For now, we use a fence to wait for completion (simple sync)
            vk::Fence fence = m_vulkanContext->getDevice().createFence({});
            
            m_vulkanContext->getQueues().compute.submit(submitInfo, fence);
            
            // Wait for completion (blocking for simplicity in this step)
            auto result = m_vulkanContext->getDevice().waitForFences(fence, VK_TRUE, UINT64_MAX);
            if (result != vk::Result::eSuccess) {
                throw std::runtime_error("Wait for fence failed");
            }
            
            m_vulkanContext->getDevice().destroyFence(fence);

            m_vulkanContext->getDevice().destroyCommandPool(cmdPool);
        }

        LOG_DEBUG("Timestep complete");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to execute timestep: {}", e.what());
        throw;
    }
}

void SimulationEngine::runFrames(uint32_t frameCount, float dt) {
    LOG_INFO("Running {} frames (dt={}s)", frameCount, dt);

    for (uint32_t frame = 0; frame < frameCount; frame++) {
        LOG_DEBUG("Frame {}/{}", frame + 1, frameCount);
        step(dt);
    }

    LOG_INFO("Simulation complete");
}

} // namespace script
// Add to end of SimulationEngine.cpp before closing namespace

void script::SimulationEngine::buildDependencyGraph() {
    LOG_INFO("Building dependency graph from {} stencils", 
             m_stencilRegistry->getStencils().size());
    
    // Clear existing graph
    m_dependencyGraph = std::make_unique<graph::DependencyGraph>();
    
    // Add each stencil as a node
    for (const auto& [name, compiled] : m_stencilRegistry->getStencils()) {
        m_dependencyGraph->addNode(
            name,
            compiled.definition.inputs,
            compiled.definition.outputs);
    }
    
    LOG_INFO("Dependency graph built with {} nodes", 
             m_dependencyGraph->getNodes().size());
}

std::vector<std::string> script::SimulationEngine::getExecutionSchedule() const {
    if (!m_dependencyGraph) {
        LOG_WARN("Dependency graph not built yet");
        return {};
    }
    
    return m_dependencyGraph->buildSchedule();
}

std::string script::SimulationEngine::exportGraphDOT() const {
    if (!m_dependencyGraph) {
        LOG_WARN("Dependency graph not built yet");
        return "";
    }
    
    return m_dependencyGraph->exportDOT();
}

void script::SimulationEngine::setExecutionOrder(const std::vector<std::string>& schedule) {
    LOG_INFO("Setting custom execution order ({} stencils)", schedule.size());
    
    // Store custom schedule
    // This would be used in step() instead of auto-generated schedule
    // For now, just log it as the implementation is a placeholder
    for (const auto& name : schedule) {
        LOG_DEBUG("  - {}", name);
    }
}
