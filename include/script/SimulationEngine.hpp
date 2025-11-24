#pragma once

#include "core/VulkanContext.hpp"
#include "core/MemoryAllocator.hpp"
#include "field/FieldRegistry.hpp"
#include "stencil/StencilRegistry.hpp"
#include "graph/DependencyGraph.hpp"
#include "graph/GraphExecutor.hpp"
#include "domain/DomainSplitter.hpp"
#include "halo/HaloManager.hpp"
#include "nanovdb_adapter/GpuGridManager.hpp"

#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>
#include <map>

namespace script {

/**
 * @brief High-level simulation engine for user scripts
 *
 * Orchestrates all subsystems (fields, stencils, execution graph, halos).
 * Provides simple API for Lua scripts to control simulation.
 */
class SimulationEngine {
public:
    /**
     * @brief Simulation configuration
     */
    struct Config {
        uint32_t gpuCount = 1;
        std::string gridFile;           // Path to NanoVDB grid
        uint32_t haloThickness = 2;
    };

    /**
     * Initialize simulation engine
     * @param config Configuration parameters
     */
    explicit SimulationEngine(const Config& config);

    /**
     * Cleanup all resources
     */
    ~SimulationEngine();

    /**
     * Add a field to the simulation
     * @param name Field name
     * @param format Vulkan format (as string: "R32F", "R32G32B32F", etc.)
     * @param initialValue Initial value (float, vec3, etc.)
     */
    void addField(const std::string& name,
                 const std::string& format,
                 const std::string& initialValue = "0.0");

    /**
     * Add a stencil (compute kernel) to the simulation
     * @param definition Stencil definition
     */
    void addStencil(const stencil::StencilDefinition& definition);

    /**
     * Execute a single simulation timestep
     * @param dt Delta time (seconds)
     */
    void step(float dt = 0.016f);

    /**
     * Run multiple timesteps
     * @param frameCount Number of frames
     * @param dt Delta time per frame
     */
    void runFrames(uint32_t frameCount, float dt = 0.016f);

    /**
     * Build dependency graph from registered stencils
     * Automatically detects RAW/WAR dependencies
     */
    void buildDependencyGraph();

    /**
     * Get current execution schedule (topologically sorted)
     * @return Ordered list of stencil names
     */
    std::vector<std::string> getExecutionSchedule() const;

    /**
     * Export dependency graph to GraphViz DOT format
     * @return DOT format string for visualization
     */
    std::string exportGraphDOT() const;

    /**
     * Override execution order (advanced usage)
     * @param schedule Custom stencil order
     */
    void setExecutionOrder(const std::vector<std::string>& schedule);

    /**
     * Get field registry (read-only access)
     */
    const field::FieldRegistry& getFieldRegistry() const { return *m_fieldRegistry; }

    /**
     * Get stencil registry (read-only access)
     */
    const stencil::StencilRegistry& getStencilRegistry() const { return *m_stencilRegistry; }

    /**
     * Get GPU count
     */
    uint32_t getGPUCount() const { return m_config.gpuCount; }

    /**
     * Check if initialized successfully
     */
    bool isInitialized() const { return m_initialized; }

private:
    Config m_config;
    bool m_initialized = false;

    // Core Vulkan components
    std::unique_ptr<core::VulkanContext> m_vulkanContext;
    std::unique_ptr<core::MemoryAllocator> m_memoryAllocator;

    // Simulation components
    std::unique_ptr<field::FieldRegistry> m_fieldRegistry;
    std::unique_ptr<stencil::StencilRegistry> m_stencilRegistry;
    std::unique_ptr<graph::DependencyGraph> m_dependencyGraph;
    std::unique_ptr<graph::GraphExecutor> m_graphExecutor;
    std::unique_ptr<domain::DomainSplitter> m_domainSplitter;
    std::unique_ptr<halo::HaloManager> m_haloManager;
    std::unique_ptr<nanovdb_adapter::GpuGridManager> m_gridManager;

    // GPU grid and domains
    nanovdb_adapter::GpuGridManager::GridResources m_gridResources;
    std::vector<domain::SubDomain> m_subDomains;

    /**
     * Initialize all subsystems
     */
    void initialize();

    /**
     * Load NanoVDB grid from file
     */
    void loadGrid();

    /**
     * Build domain decomposition for current grid
     */
    void decomposeDomain();

    /**
     * Parse Vulkan format string to vk::Format
     */
    vk::Format parseFormat(const std::string& formatStr);
};

} // namespace script
