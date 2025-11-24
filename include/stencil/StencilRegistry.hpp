#pragma once

#include "stencil/StencilDefinition.hpp"
#include "stencil/ShaderGenerator.hpp"
#include "stencil/PipelineCache.hpp"
#include "core/VulkanContext.hpp"
#include "field/FieldRegistry.hpp"

#include <vulkan/vulkan.hpp>
#include <unordered_map>
#include <memory>

namespace stencil {

/**
 * @brief Registry of compiled stencil pipelines
 *
 * Manages stencil compilation, caching, and retrieval.
 * Handles SPIR-V generation and Vulkan pipeline creation.
 */
class StencilRegistry {
public:
    /**
     * Initialize stencil registry
     * @param context Vulkan context
     * @param fieldRegistry Field registry for validation
     * @param cacheDir Directory for SPIR-V cache (default: ~/.fluidloom/shader_cache)
     */
    StencilRegistry(const core::VulkanContext& context,
                   const field::FieldRegistry& fieldRegistry,
                   const std::filesystem::path& cacheDir = "");

    ~StencilRegistry();

    /**
     * Register and compile a new stencil
     * @param definition Stencil definition
     * @return Compiled stencil
     */
    const CompiledStencil& registerStencil(const StencilDefinition& definition);

    /**
     * Get a compiled stencil by name
     * @param name Stencil name
     * @throws std::runtime_error if stencil not found
     */
    const CompiledStencil& getStencil(const std::string& name) const;

    /**
     * Check if stencil exists
     */
    bool hasStencil(const std::string& name) const;

    /**
     * Get all stencils
     */
    const std::unordered_map<std::string, CompiledStencil>& getStencils() const {
        return m_stencils;
    }

    /**
     * Create pipeline layout for stencils
     * @return vk::PipelineLayout
     */
    vk::PipelineLayout createPipelineLayout();

public:
    const core::VulkanContext& m_context;
    const field::FieldRegistry& m_fieldRegistry;
    ShaderGenerator m_shaderGenerator;
    PipelineCache m_pipelineCache;

    // Compiled stencils: name -> CompiledStencil
    std::unordered_map<std::string, CompiledStencil> m_stencils;

    // Shared pipeline layout for all stencils
    vk::PipelineLayout m_pipelineLayout;

    // Vulkan pipeline cache for fast recompilation
    vk::PipelineCache m_vkPipelineCache;

    /**
     * Compile GLSL source to SPIR-V (stub - full implementation requires DXC)
     * @param glslSource GLSL shader source
     * @param entryPoint Entry function name
     * @return SPIR-V bytecode
     */
    std::vector<uint32_t> compileToSPIRV(const std::string& glslSource,
                                         const std::string& entryPoint = "main");

    /**
     * Create compute pipeline from SPIR-V
     */
    vk::Pipeline createComputePipeline(const std::vector<uint32_t>& spirvCode);

    /**
     * Validate stencil definition
     */
    void validateStencil(const StencilDefinition& definition);
};

} // namespace stencil
