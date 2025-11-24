#include "stencil/StencilRegistry.hpp"
#include "core/Logger.hpp"

#include <stdexcept>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <filesystem> // Added for std::filesystem::path

namespace stencil {

StencilRegistry::StencilRegistry(const core::VulkanContext& context,
                                 const field::FieldRegistry& fieldRegistry,
                                 const std::filesystem::path& cacheDir)
    : m_context(context),
      m_fieldRegistry(fieldRegistry),
      m_shaderGenerator(fieldRegistry),
      m_pipelineCache(cacheDir.empty() ? 
                      std::filesystem::path(std::getenv("HOME")) / ".fluidloom" / "shader_cache" :
                      cacheDir) {
    LOG_INFO("Initializing StencilRegistry with cache: {}", m_pipelineCache.getCacheDir().string());

    // Create pipeline layout
    m_pipelineLayout = createPipelineLayout();

    // Create Vulkan pipeline cache for fast recompilation
    vk::PipelineCacheCreateInfo cacheInfo(
        {}, // flags
        0, nullptr // initialDataSize, pInitialData
    );
    try {
        m_vkPipelineCache = m_context.getDevice().createPipelineCache(cacheInfo);
        LOG_DEBUG("Vulkan pipeline cache created");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create Vulkan pipeline cache: {}", e.what());
        throw;
    }
}

StencilRegistry::~StencilRegistry() {
    if (m_vkPipelineCache) {
        m_context.getDevice().destroyPipelineCache(m_vkPipelineCache);
    }
    if (m_pipelineLayout) {
        m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
    }
    LOG_DEBUG("StencilRegistry destroyed");
}

vk::PipelineLayout StencilRegistry::createPipelineLayout() {
    LOG_DEBUG("Creating pipeline layout for stencils");

    // Push constant range: large enough for field addresses + metadata
    vk::PushConstantRange pushConstantRange(
        vk::ShaderStageFlagBits::eCompute,
        0, // offset
        256 // size
    );
    vk::PipelineLayoutCreateInfo layoutInfo(
        {}, // flags
        0, nullptr, // pSetLayouts
        1, &pushConstantRange // pPushConstantRanges
    );

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
    LOG_INFO("Compiling GLSL to SPIR-V using glslc");

    // Create temporary files
    std::string uuid = "shader_" + std::to_string(std::rand()); // Simple random ID
    std::string glslFile = "/tmp/" + uuid + ".comp";
    std::string spvFile = "/tmp/" + uuid + ".spv";

    // Write GLSL to file
    {
        std::ofstream out(glslFile);
        if (!out) {
            throw std::runtime_error("Failed to create temporary GLSL file");
        }
        out << glslSource;
    }

    // Build command: glslc -fshader-stage=compute -o output.spv input.glsl
    std::string command = "/opt/homebrew/bin/glslc -fshader-stage=compute -o " + spvFile + " " + glslFile;
    
    // Execute command
    int ret = std::system(command.c_str());
    
    // Cleanup GLSL file
    std::remove(glslFile.c_str());

    if (ret != 0) {
        std::string error = "Shader compilation failed with code " + std::to_string(ret);
        LOG_ERROR(error);
        throw std::runtime_error(error);
    }

    // Read SPIR-V back
    std::vector<uint32_t> spirv;
    {
        std::ifstream in(spvFile, std::ios::binary | std::ios::ate);
        if (!in) {
            throw std::runtime_error("Failed to open compiled SPIR-V file");
        }

        size_t fileSize = in.tellg();
        if (fileSize % 4 != 0) {
            throw std::runtime_error("Invalid SPIR-V file size (not multiple of 4)");
        }

        spirv.resize(fileSize / 4);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(spirv.data()), fileSize);
    }

    // Cleanup SPIR-V file
    std::remove(spvFile.c_str());

    LOG_DEBUG("Shader compiled successfully ({} bytes)", spirv.size() * 4);
    return spirv;
}

vk::Pipeline StencilRegistry::createComputePipeline(const std::vector<uint32_t>& spirvCode) {
    LOG_DEBUG("Creating compute pipeline from SPIR-V ({} words)", spirvCode.size());

    // Create shader module
    vk::ShaderModuleCreateInfo moduleInfo(
        {}, // flags
        spirvCode.size() * sizeof(uint32_t),
        spirvCode.data()
    );

    vk::ShaderModule shaderModule;
    try {
        shaderModule = m_context.getDevice().createShaderModule(moduleInfo);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create shader module: {}", e.what());
        throw;
    }

    // Create pipeline
    vk::PipelineShaderStageCreateInfo shaderStageInfo(
        {}, // flags
        vk::ShaderStageFlagBits::eCompute,
        shaderModule,
        "main",
        nullptr
    );
    vk::ComputePipelineCreateInfo pipelineInfo(
        {}, // flags
        shaderStageInfo,
        m_pipelineLayout
    );
    vk::Pipeline pipeline;
    try {
        auto result = m_context.getDevice().createComputePipeline(
            m_vkPipelineCache, pipelineInfo);
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

    // Check disk cache first
    std::vector<uint32_t> spirvCode = m_pipelineCache.load(definition.name, glslSource);
    
    if (spirvCode.empty()) {
        // Cache miss - compile to SPIR-V
        LOG_DEBUG("Cache miss for '{}', compiling...", definition.name);
        spirvCode = compileToSPIRV(glslSource);
        
        // Save to disk cache
        m_pipelineCache.save(definition.name, glslSource, spirvCode);
    }

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
