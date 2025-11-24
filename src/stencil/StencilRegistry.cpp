#include "stencil/StencilRegistry.hpp"
#include "core/Logger.hpp"

#include <stdexcept>

namespace stencil {

StencilRegistry::StencilRegistry(const core::VulkanContext& context,
                                 const field::FieldRegistry& fieldRegistry)
    : m_context(context),
      m_fieldRegistry(fieldRegistry),
      m_shaderGenerator(fieldRegistry) {
    LOG_INFO("Initializing StencilRegistry");

    // Create pipeline layout
    m_pipelineLayout = createPipelineLayout();

    // Create pipeline cache
    vk::PipelineCacheCreateInfo cacheInfo{
        .initialDataSize = 0,
        .pInitialData = nullptr
    };

    try {
        m_pipelineCache = m_context.getDevice().createPipelineCache(cacheInfo);
        LOG_DEBUG("Pipeline cache created");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create pipeline cache: {}", e.what());
        throw;
    }
}

StencilRegistry::~StencilRegistry() {
    if (m_pipelineCache) {
        m_context.getDevice().destroyPipelineCache(m_pipelineCache);
    }
    if (m_pipelineLayout) {
        m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
    }
    LOG_DEBUG("StencilRegistry destroyed");
}

vk::PipelineLayout StencilRegistry::createPipelineLayout() {
    LOG_DEBUG("Creating pipeline layout for stencils");

    // Push constant range: large enough for field addresses + metadata
    vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = 256  // Safe upper limit for push constants
    };

    vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = 0,                    // No descriptor sets (bindless)
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };

    try {
        auto layout = m_context.getDevice().createPipelineLayout(layoutInfo);
        LOG_DEBUG("Pipeline layout created");
        return layout;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create pipeline layout: {}", e.what());
        throw;
    }
}

void StencilRegistry::validateStencil(const StencilDefinition& definition) {
    LOG_DEBUG("Validating stencil: '{}'", definition.name);

    if (definition.name.empty()) {
        throw std::runtime_error("Stencil name cannot be empty");
    }

    if (definition.inputs.empty() && definition.outputs.empty()) {
        throw std::runtime_error("Stencil must have inputs or outputs");
    }

    // Validate field names
    for (const auto& fieldName : definition.inputs) {
        if (!m_fieldRegistry.hasField(fieldName)) {
            throw std::runtime_error("Input field not found: " + fieldName);
        }
    }

    for (const auto& fieldName : definition.outputs) {
        if (!m_fieldRegistry.hasField(fieldName)) {
            throw std::runtime_error("Output field not found: " + fieldName);
        }
    }

    LOG_DEBUG("Stencil validation passed");
}

std::vector<uint32_t> StencilRegistry::compileToSPIRV(const std::string& glslSource,
                                                       const std::string& entryPoint) {
    LOG_INFO("Compiling GLSL to SPIR-V (stub - DXC integration pending)");

    // STUB: This would require DXC (DirectXShaderCompiler) integration
    // For now, return empty SPIR-V that will fail pipeline creation
    // Full implementation requires:
    // 1. Initialize DXC compiler
    // 2. Create DxcBuffer from shader source
    // 3. Set compilation flags for SPIR-V generation
    // 4. Call compiler->Compile()
    // 5. Extract SPIR-V bytecode

    LOG_WARN("DXC integration not yet implemented - returning stub SPIR-V");

    // Minimal valid SPIR-V header (will fail validation)
    std::vector<uint32_t> spirvStub = {
        0x07230203,  // SPIR-V magic number
        0x00010300,  // Version 1.3
        0x00070000,  // Generator (placeholder)
        0x00000001,  // Bound
        0x00000000   // Schema (unused)
    };

    return spirvStub;
}

vk::Pipeline StencilRegistry::createComputePipeline(const std::vector<uint32_t>& spirvCode) {
    LOG_DEBUG("Creating compute pipeline from SPIR-V ({} words)", spirvCode.size());

    // Create shader module
    vk::ShaderModuleCreateInfo moduleInfo{
        .codeSize = spirvCode.size() * sizeof(uint32_t),
        .pCode = spirvCode.data()
    };

    vk::ShaderModule shaderModule;
    try {
        shaderModule = m_context.getDevice().createShaderModule(moduleInfo);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create shader module: {}", e.what());
        throw;
    }

    // Create pipeline
    vk::PipelineShaderStageCreateInfo shaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = shaderModule,
        .pName = "main"
    };

    vk::ComputePipelineCreateInfo pipelineInfo{
        .stage = shaderStageInfo,
        .layout = m_pipelineLayout
    };

    vk::Pipeline pipeline;
    try {
        auto result = m_context.getDevice().createComputePipeline(
            m_pipelineCache, pipelineInfo);
        pipeline = result.value;
        LOG_DEBUG("Compute pipeline created");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create compute pipeline: {}", e.what());
        m_context.getDevice().destroyShaderModule(shaderModule);
        throw;
    }

    // Cleanup shader module (pipeline takes ownership)
    m_context.getDevice().destroyShaderModule(shaderModule);

    return pipeline;
}

const CompiledStencil& StencilRegistry::registerStencil(const StencilDefinition& definition) {
    LOG_INFO("Registering stencil: '{}'", definition.name);

    // Validate
    validateStencil(definition);

    // Check for duplicates
    if (m_stencils.count(definition.name) > 0) {
        throw std::runtime_error("Stencil already registered: " + definition.name);
    }

    // Generate GLSL
    std::string glslSource = m_shaderGenerator.generateComputeShader(definition);

    // Compile to SPIR-V
    std::vector<uint32_t> spirvCode = compileToSPIRV(glslSource);

    // Create pipeline
    vk::Pipeline pipeline = createComputePipeline(spirvCode);

    // Store compiled stencil
    CompiledStencil compiled{
        .definition = definition,
        .pipeline = pipeline,
        .layout = m_pipelineLayout,
        .spirvCode = spirvCode,
        .glslSource = glslSource
    };

    auto& storedStencil = m_stencils[definition.name] = compiled;

    LOG_INFO("Stencil '{}' registered and compiled", definition.name);

    return storedStencil;
}

const CompiledStencil& StencilRegistry::getStencil(const std::string& name) const {
    auto it = m_stencils.find(name);
    if (it == m_stencils.end()) {
        throw std::runtime_error("Stencil not found: " + name);
    }
    return it->second;
}

bool StencilRegistry::hasStencil(const std::string& name) const {
    return m_stencils.count(name) > 0;
}

} // namespace stencil
