/*
  ______ _       _     _ _                     
 |  ____| |     (_)   | | |                    
 | |__  | |_   _ _  __| | |     ___   ___  _ __ ___ 
 |  __| | | | | | |/ _` | |    / _ \ / _ \| '_ ` _ \
 | |    | | |_| | | (_| | |___| (_) | (_) | | | | | |
 |_|    |_|\__,_|_|\__,_|______\___/ \___/|_| |_| |_|
                                                     
  FluidLoom - GPU-Accelerated Fluid Simulation Engine
  
  Author: zombie aka Karthik Thyagarajan
*/

#include "script/SimulationEngine.hpp"
#include "nanovdb_adapter/GridLoader.hpp"
#include "core/Logger.hpp"

#include <nanovdb/tools/CreateNanoGrid.h>
#include <nanovdb/tools/CreatePrimitives.h>
#include <nanovdb/tools/GridBuilder.h>
#include <nanovdb/io/IO.h>
#include <nanovdb/util/GridBuilder.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>

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

void SimulationEngine::configureDomain(const DomainConfig& config) {
    LOG_INFO("Configuring domain: {}x{}x{} cells, dx={}",
             config.nx, config.ny, config.nz, config.dx);

    m_config.domain = config;

    // Log geometry if specified
    if (!config.geometryType.empty()) {
        LOG_INFO("Geometry: {}", config.geometryType);
    }

    // Log boundary conditions
    if (!config.boundaryX0.empty()) LOG_INFO("Boundary -X: {}", config.boundaryX0);
    if (!config.boundaryX1.empty()) LOG_INFO("Boundary +X: {}", config.boundaryX1);
    if (!config.boundaryY0.empty()) LOG_INFO("Boundary -Y: {}", config.boundaryY0);
    if (!config.boundaryY1.empty()) LOG_INFO("Boundary +Y: {}", config.boundaryY1);
    if (!config.boundaryZ0.empty()) LOG_INFO("Boundary -Z: {}", config.boundaryZ0);
    if (!config.boundaryZ1.empty()) LOG_INFO("Boundary +Z: {}", config.boundaryZ1);
}

nanovdb::GridHandle<nanovdb::HostBuffer> SimulationEngine::buildUniformGrid() {
    const auto& cfg = m_config.domain;

    LOG_INFO("Building uniform grid: {}x{}x{} (dx={})", cfg.nx, cfg.ny, cfg.nz, cfg.dx);

    // Create a box covering the entire domain
    nanovdb::CoordBBox bbox(
        nanovdb::Coord(0, 0, 0),
        nanovdb::Coord(cfg.nx - 1, cfg.ny - 1, cfg.nz - 1)
    );

    // Count active voxels based on geometry
    uint32_t totalVoxels = 0;
    for (uint32_t k = 0; k < cfg.nz; k++) {
        for (uint32_t j = 0; j < cfg.ny; j++) {
            for (uint32_t i = 0; i < cfg.nx; i++) {
                bool isActive = true;

                // Check if inside geometry (mark as inactive)
                if (!cfg.geometryType.empty()) {
                    if (cfg.geometryType == "sphere") {
                        // geometryParams: [cx, cy, cz, radius, ...]
                        float cx = cfg.geometryParams[0];
                        float cy = cfg.geometryParams[1];
                        float cz = cfg.geometryParams[2];
                        float radius = cfg.geometryParams[3];

                        float dx = (i - cx);
                        float dy = (j - cy);
                        float dz = (k - cz);
                        float distSq = dx*dx + dy*dy + dz*dz;

                        if (distSq < radius * radius) {
                            isActive = false; // Inside sphere - inactive
                        }
                    } else if (cfg.geometryType == "box") {
                        // geometryParams: [xmin, ymin, zmin, xmax, ymax, zmax]
                        if (i >= cfg.geometryParams[0] && i < cfg.geometryParams[3] &&
                            j >= cfg.geometryParams[1] && j < cfg.geometryParams[4] &&
                            k >= cfg.geometryParams[2] && k < cfg.geometryParams[5]) {
                            isActive = false; // Inside box - inactive
                        }
                    }
                }

                if (isActive) {
                    totalVoxels++;
                }
            }
        }
    }

    LOG_INFO("Uniform grid created: {} active voxels out of {} total",
             totalVoxels, cfg.nx * cfg.ny * cfg.nz);

    // For now, use a dense fog volume as the base grid
    // This creates a uniform density field
    auto handle = nanovdb::tools::createFogVolumeSphere<float>(
        cfg.nx / 2.0,  // radius (half the domain)
        nanovdb::Vec3d(cfg.nx / 2.0, cfg.ny / 2.0, cfg.nz / 2.0),  // center
        cfg.dx,  // voxel size
        cfg.dx * 3.0,  // half-width
        nanovdb::Vec3d(0.0)  // origin
    );

    return handle;
}

void SimulationEngine::decomposeDomain() {
    LOG_INFO("Decomposing domain for {} GPUs", m_config.gpuCount);

    try {
        nanovdb::GridHandle<nanovdb::HostBuffer> hostHandle;

        // Load grid if file specified, otherwise create from domain config
        if (m_config.gridFile.empty()) {
            LOG_INFO("No grid file specified, creating grid from domain configuration");
            hostHandle = buildUniformGrid();
        } else {
            // Load grid from file
            if (m_gridResources.activeVoxelCount == 0) {
                loadGrid();
            }
            hostHandle = nanovdb_adapter::GridLoader::load(m_config.gridFile);
            LOG_INFO("Loaded grid from file: {}", m_config.gridFile);
        }

        // Decompose based on GPU count
        if (m_config.gpuCount == 1) {
            LOG_INFO("Single GPU mode - creating single domain without decomposition");

            // Create a single domain covering the entire grid
            auto* grid = hostHandle.grid<float>();
            if (!grid) {
                throw std::runtime_error("Invalid grid handle");
            }

            domain::SubDomain singleDomain;
            singleDomain.gpuIndex = 0;
            singleDomain.bounds = grid->indexBBox();

            // Count active voxels
            uint32_t voxelCount = 0;
            for (auto iter = grid->tree().root().cbeginValueOn(); iter; ++iter) {
                voxelCount++;
            }
            singleDomain.activeVoxelCount = voxelCount;

            m_subDomains.push_back(singleDomain);
            LOG_INFO("Single domain created: {} active voxels", voxelCount);
        } else {
            LOG_INFO("Multi-GPU mode - decomposing into {} domains", m_config.gpuCount);
            m_subDomains = m_domainSplitter->split(hostHandle);
            LOG_INFO("Domain decomposed into {} sub-domains", m_subDomains.size());
        }

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

    // Auto-initialize domains on first step if not already done
    if (m_subDomains.empty()) {
        LOG_INFO("Domains not initialized, auto-initializing before first timestep");
        decomposeDomain();
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

void script::SimulationEngine::writeVDB(const std::string& fieldName, const std::string& filepath) {
    LOG_INFO("Writing field '{}' to VDB file: {}", fieldName, filepath);

    // Get field descriptor
    const auto& fields = m_fieldRegistry->getFields();
    auto it = fields.find(fieldName);
    if (it == fields.end()) {
        std::string error = "Field '" + fieldName + "' not found";
        LOG_ERROR(error);
        throw std::runtime_error(error);
    }

    const auto& fieldDesc = it->second;
    uint32_t activeVoxelCount = m_gridResources.activeVoxelCount;

    if (activeVoxelCount == 0) {
        LOG_WARN("No active voxels to write");
        return;
    }

    // Download field data
    std::vector<uint8_t> rawData = downloadBuffer(fieldDesc.buffer, activeVoxelCount * sizeof(float));
    const float* fieldData = reinterpret_cast<const float*>(rawData.data());

    // Download coordinates
    std::vector<uint8_t> rawCoords = downloadBuffer(m_gridResources.lutCoords, activeVoxelCount * sizeof(nanovdb::Coord));
    const nanovdb::Coord* coords = reinterpret_cast<const nanovdb::Coord*>(rawCoords.data());

    // Create NanoVDB grid using tools::build::Grid
    LOG_DEBUG("Building NanoVDB grid");
    nanovdb::tools::build::Grid<float> builder(0.0f);  // Background value = 0.0

    for (uint32_t i = 0; i < activeVoxelCount; ++i) {
        builder.setValue(coords[i], fieldData[i]);
    }

    // Convert to NanoVDB format
    LOG_DEBUG("Converting to NanoVDB format");
    auto handle = nanovdb::tools::createNanoGrid(builder);

    // Set grid name
    auto* gridData = handle.template grid<float>();
    if (gridData) {
        gridData->setGridName(fieldName.c_str());
    }

    // Write to file
    LOG_DEBUG("Writing to file: {}", filepath);
    nanovdb::io::writeGrid(filepath, handle);

    LOG_INFO("Successfully wrote VDB file: {}", filepath);
}

void script::SimulationEngine::writeVTK(const std::string& fieldName, const std::string& filepath) {
    LOG_INFO("Writing field '{}' to VTK file: {}", fieldName, filepath);

    // Get field descriptor
    const auto& fields = m_fieldRegistry->getFields();
    auto it = fields.find(fieldName);
    if (it == fields.end()) {
        std::string error = "Field '" + fieldName + "' not found";
        LOG_ERROR(error);
        throw std::runtime_error(error);
    }

    const auto& fieldDesc = it->second;
    uint32_t activeVoxelCount = m_gridResources.activeVoxelCount;

    if (activeVoxelCount == 0) {
        LOG_WARN("No active voxels to write");
        return;
    }

    // Download field data
    std::vector<uint8_t> rawData = downloadBuffer(fieldDesc.buffer, activeVoxelCount * sizeof(float));
    const float* fieldData = reinterpret_cast<const float*>(rawData.data());

    // Download coordinates
    std::vector<uint8_t> rawCoords = downloadBuffer(m_gridResources.lutCoords, activeVoxelCount * sizeof(nanovdb::Coord));
    const nanovdb::Coord* coords = reinterpret_cast<const nanovdb::Coord*>(rawCoords.data());

    std::ofstream file(filepath);
    if (!file) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }

    // Write VTK Unstructured Grid (XML)
    file << "<?xml version=\"1.0\"?>\n";
    file << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    file << "  <UnstructuredGrid>\n";
    file << "    <Piece NumberOfPoints=\"" << activeVoxelCount << "\" NumberOfCells=\"" << activeVoxelCount << "\">\n";

    // Points
    file << "      <Points>\n";
    file << "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    float dx = m_config.domain.dx;
    for (uint32_t i = 0; i < activeVoxelCount; ++i) {
        file << coords[i][0] * dx << " " << coords[i][1] * dx << " " << coords[i][2] * dx << " ";
    }
    file << "\n        </DataArray>\n";
    file << "      </Points>\n";

    // Cells (Vertex type for point cloud)
    file << "      <Cells>\n";
    file << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
    for (uint32_t i = 0; i < activeVoxelCount; ++i) file << i << " ";
    file << "\n        </DataArray>\n";
    file << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    for (uint32_t i = 0; i < activeVoxelCount; ++i) file << i + 1 << " ";
    file << "\n        </DataArray>\n";
    file << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    for (uint32_t i = 0; i < activeVoxelCount; ++i) file << "1 "; // VTK_VERTEX
    file << "\n        </DataArray>\n";
    file << "      </Cells>\n";

    // Point Data (The Field)
    file << "      <PointData Scalars=\"" << fieldName << "\">\n";
    file << "        <DataArray type=\"Float32\" Name=\"" << fieldName << "\" format=\"ascii\">\n";
    for (uint32_t i = 0; i < activeVoxelCount; ++i) {
        file << fieldData[i] << " ";
    }
    file << "\n        </DataArray>\n";
    file << "      </PointData>\n";

    file << "    </Piece>\n";
    file << "  </UnstructuredGrid>\n";
    file << "</VTKFile>\n";

    LOG_INFO("Successfully wrote VTK file: {}", filepath);
}

void script::SimulationEngine::dispatch(const std::string& stencilName, uint32_t level, float dt) {
    // Ensure domain is initialized
    if (m_subDomains.empty()) {
        LOG_WARN("Domain not initialized, calling decomposeDomain()");
        decomposeDomain();
    }
    
    // Find level info
    // We need to search m_gridResources.levels
    // This is a linear search but the vector is small (depth < 10)
    
    uint32_t startIndex = 0;
    uint32_t count = 0;
    bool found = false;

    for (const auto& info : m_gridResources.levels) {
        if (info.level == level) {
            startIndex = info.startIndex;
            count = info.count;
            found = true;
            break;
        }
    }

    if (!found) {
        // If level not found, it might be empty. Just return.
        // Or if level 0 is requested but not explicit, maybe it's the whole grid?
        // No, GpuGridManager guarantees levels are populated if active.
        // If not found, it means no active voxels at this level.
        LOG_DEBUG("Skipping dispatch for stencil '{}' at level {}: No active voxels", stencilName, level);
        return;
    }

    LOG_DEBUG("Dispatching '{}' at level {} (Start: {}, Count: {})", stencilName, level, startIndex, count);

    // Execute immediately
    // Create command buffer
    vk::CommandPool cmdPool = m_vulkanContext->createCommandPool(
        m_vulkanContext->getQueues().computeFamily);

    vk::CommandBufferAllocateInfo allocInfo(
        cmdPool,
        vk::CommandBufferLevel::ePrimary,
        1
    );
    vk::CommandBuffer cmd = m_vulkanContext->getDevice().allocateCommandBuffers(allocInfo)[0];

    // Record
    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmd.begin(beginInfo);

    // We assume single domain for now for simplicity in this method
    // In multi-GPU, we would need to dispatch on all domains
    if (!m_subDomains.empty()) {
        // Use the first domain (or iterate all)
        // For now, iterate all domains
        for (const auto& domain : m_subDomains) {
             m_graphExecutor->dispatchStencil(cmd, stencilName, *m_stencilRegistry, domain, count, startIndex, dt);
        }
    }

    cmd.end();

    // Submit
    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vk::Fence fence = m_vulkanContext->getDevice().createFence({});
    m_vulkanContext->getQueues().compute.submit(submitInfo, fence);

    // Wait
    auto result = m_vulkanContext->getDevice().waitForFences(fence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Wait for fence failed in dispatch");
    }

    m_vulkanContext->getDevice().destroyFence(fence);
    m_vulkanContext->getDevice().destroyCommandPool(cmdPool);
}

std::vector<uint8_t> script::SimulationEngine::downloadBuffer(const core::MemoryAllocator::Buffer& buffer, size_t size) {
    std::vector<uint8_t> hostData(size);
    void* mapped = m_memoryAllocator->mapBuffer(buffer);
    std::memcpy(hostData.data(), mapped, size);
    m_memoryAllocator->unmapBuffer(buffer);
    return hostData;
}
