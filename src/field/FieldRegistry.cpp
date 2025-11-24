#include "field/FieldRegistry.hpp"
#include "core/VulkanContext.hpp"
#include "core/Logger.hpp"

#include <stdexcept>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstdio>

namespace field {

FieldRegistry::FieldRegistry(core::VulkanContext& context,
                             core::MemoryAllocator& allocator,
                             uint32_t activeVoxelCount)
    : m_context(context),
      m_allocator(allocator),
      m_activeVoxelCount(activeVoxelCount) {
    LOG_INFO("Initializing FieldRegistry with {} active voxels", activeVoxelCount);

    // Create command pool for fill operations
    m_computeCommandPool = context.createCommandPool(
        context.getQueues().computeFamily,
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    // Allocate BDA table (GPU-visible, host-mapped)
    m_bdaTableBuffer = allocator.createBuffer(
        MAX_FIELDS * sizeof(uint64_t),
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eShaderDeviceAddress,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    // Map BDA table
    if (m_bdaTableBuffer.mappedData) {
        m_bdaTableMapped = static_cast<uint64_t*>(m_bdaTableBuffer.mappedData);
        memset(m_bdaTableMapped, 0, MAX_FIELDS * sizeof(uint64_t));
    } else {
        throw std::runtime_error("BDA table buffer not host-accessible");
    }

    LOG_DEBUG("FieldRegistry created with BDA table at 0x{:x}",
              static_cast<uint64_t>(m_bdaTableBuffer.deviceAddress));
}

FieldRegistry::~FieldRegistry() {
    // Cleanup pipelines
    if (m_fillLayout) {
        m_context.getDevice().destroyPipelineLayout(m_fillLayout);
    }
    if (m_fillPipeline) {
        m_context.getDevice().destroyPipeline(m_fillPipeline);
    }

    // Cleanup command pool
    if (m_computeCommandPool) {
        m_context.getDevice().destroyCommandPool(m_computeCommandPool);
    }

    // Cleanup BDA table buffer
    m_allocator.destroyBuffer(m_bdaTableBuffer);

    LOG_DEBUG("FieldRegistry destroyed");
}

const FieldDesc& FieldRegistry::registerField(const std::string& name,
                                              vk::Format format,
                                              const void* initialValue) {
    LOG_INFO("Registering field: '{}'", name);

    // Check for duplicates
    if (m_fields.count(name) > 0) {
        throw std::runtime_error("Field already exists: " + name);
    }

    // Check field limit
    if (m_nextDescriptorIndex >= MAX_FIELDS) {
        throw std::runtime_error("Maximum number of fields exceeded");
    }

    // Determine element size from format
    uint32_t elementSize = 0;
    switch (format) {
        case vk::Format::eR32Sfloat:
        case vk::Format::eR32Sint:
            elementSize = sizeof(float);
            break;
        case vk::Format::eR32G32Sfloat:
        case vk::Format::eR32G32Sint:
            elementSize = 2 * sizeof(float);
            break;
        case vk::Format::eR32G32B32Sfloat:
        case vk::Format::eR32G32B32Sint:
            elementSize = 3 * sizeof(float);
            break;
        case vk::Format::eR32G32B32A32Sfloat:
        case vk::Format::eR32G32B32A32Sint:
            elementSize = 4 * sizeof(float);
            break;
        default:
            throw std::runtime_error("Unsupported field format");
    }

    // Create field descriptor
    FieldDesc desc;
    desc.name = name;
    desc.format = format;
    desc.elementSize = elementSize;
    desc.descriptorIndex = m_nextDescriptorIndex++;

    // Allocate GPU buffer for field data
    vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(m_activeVoxelCount) * elementSize;
    desc.buffer = m_allocator.createBuffer(
        bufferSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eShaderDeviceAddress |
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eTransferSrc);

    desc.deviceAddress = m_allocator.getBufferAddress(desc.buffer);

    // Update BDA table
    m_bdaTableMapped[desc.descriptorIndex] = static_cast<uint64_t>(desc.deviceAddress);

    // Flush BDA table if not coherent
    // Note: VMA allocates with VMA_MEMORY_USAGE_CPU_TO_GPU which is typically coherent on modern GPUs
    // For safety, could add explicit flush here if needed

    // Store field
    auto& storedDesc = m_fields[name] = desc;

    LOG_DEBUG("Field '{}' registered: format={}, elementSize={}, bufferSize={}, descriptorIndex={}",
              name, vk::to_string(format), elementSize, bufferSize, desc.descriptorIndex);

    // Initialize field to zero or provided value
    if (initialValue) {
        initializeField(name, initialValue);
    } else {
        // Zero-initialize by default
        vk::CommandBuffer cmd = m_context.beginSingleTimeCommands(m_computeCommandPool);
        cmd.fillBuffer(storedDesc.buffer.handle, 0, bufferSize, 0);
        m_context.endSingleTimeCommands(cmd, m_computeCommandPool, m_context.getQueues().compute);
    }

    return storedDesc;
}

const FieldDesc& FieldRegistry::getField(const std::string& name) const {
    auto it = m_fields.find(name);
    if (it == m_fields.end()) {
        throw std::runtime_error("Field not found: " + name);
    }
    return it->second;
}

bool FieldRegistry::hasField(const std::string& name) const {
    return m_fields.count(name) > 0;
}

vk::DeviceAddress FieldRegistry::getBDATableAddress() const {
    return m_bdaTableBuffer.deviceAddress;
}

std::string FieldRegistry::generateGLSLHeader() const {
    std::stringstream ss;

    // Include necessary extensions
    ss << R"(
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_separate_shader_objects : require
)";

    // Generate buffer reference types for each field
    ss << "\n// --- Field Buffer References ---\n";
    for (const auto& [name, desc] : m_fields) {
        ss << "layout(buffer_reference, scalar) buffer " << name << "_Buffer { "
           << desc.getGLSLType() << " data[]; };\n";
    }

    // Generate push constant structure with all field addresses
    ss << "\n// --- Push Constants: Field Table ---\n";
    ss << "layout(push_constant) uniform FieldTable {\n";
    for (const auto& [name, desc] : m_fields) {
        ss << "    uint64_t " << name << ";\n";
    }
    ss << "} fields;\n";

    // Generate accessor macros
    ss << "\n// --- Field Accessor Macros ---\n";
    for (const auto& [name, desc] : m_fields) {
        ss << "#define Read_" << name << "(idx) (" << name << "_Buffer(fields." << name << ").data[idx])\n";
        ss << "#define Write_" << name << "(idx, val) (" << name << "_Buffer(fields." << name << ").data[idx] = val)\n";
    }

    ss << "\n";
    return ss.str();
}

void FieldRegistry::createFillPipeline() {
    LOG_DEBUG("Creating fill pipeline");

    // Generate GLSL for fill shader
    std::string glslSource = R"(
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform PushConstants {
    uint64_t bufferAddr;
    uint32_t elementCount;
    float value;
} pc;

layout(buffer_reference, scalar) buffer DataBuffer {
    float data[];
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.elementCount) return;

    DataBuffer(pc.bufferAddr).data[idx] = pc.value;
}
)";

    // Compile to SPIR-V using StencilRegistry's helper (we need to expose it or duplicate it)
    // Since StencilRegistry depends on FieldRegistry, we can't easily use it here without circular dep.
    // For now, we'll duplicate the compile logic or use a static helper.
    // Actually, let's use a temporary StencilRegistry just for compilation if possible,
    // or better, move the compile logic to a common utility.
    // Given the constraints, I will duplicate the compile logic here for simplicity as per plan.
    
    // ... Duplicated compile logic from StencilRegistry ...
    // Wait, I can't easily duplicate it without including headers.
    // Let's assume I can use a helper or just implement it here.
    
    // Create temporary files
    std::string uuid = "fill_shader_" + std::to_string(std::rand());
    std::string glslFile = "/tmp/" + uuid + ".comp";
    std::string spvFile = "/tmp/" + uuid + ".spv";

    {
        std::ofstream out(glslFile);
        out << glslSource;
    }

    std::string command = "/opt/homebrew/bin/glslc -fshader-stage=compute -o " + spvFile + " " + glslFile;
    int ret = std::system(command.c_str());
    std::remove(glslFile.c_str());

    if (ret != 0) {
        throw std::runtime_error("Fill shader compilation failed");
    }

    std::vector<uint32_t> spirv;
    {
        std::ifstream in(spvFile, std::ios::binary | std::ios::ate);
        size_t fileSize = in.tellg();
        spirv.resize(fileSize / 4);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(spirv.data()), fileSize);
    }
    std::remove(spvFile.c_str());

    // Create shader module
    vk::ShaderModuleCreateInfo moduleInfo(
        {}, // flags
        spirv.size() * sizeof(uint32_t),
        spirv.data()
    );
    vk::ShaderModule shaderModule = m_context.getDevice().createShaderModule(moduleInfo);

    // Create pipeline layout
    vk::PushConstantRange pushRange(
        vk::ShaderStageFlagBits::eCompute,
        0, // offset
        sizeof(uint64_t) + sizeof(uint32_t) + sizeof(float) // size
    );

    vk::PipelineLayoutCreateInfo layoutInfo(
        {}, // flags
        0, nullptr, // setLayoutCount, pSetLayouts
        1, &pushRange // pushConstantRangeCount, pPushConstantRanges
    );
    m_fillLayout = m_context.getDevice().createPipelineLayout(layoutInfo);

    // Create pipeline
    vk::PipelineShaderStageCreateInfo stageInfo(
        {}, // flags
        vk::ShaderStageFlagBits::eCompute,
        shaderModule,
        "main",
        nullptr // pSpecializationInfo
    );

    vk::ComputePipelineCreateInfo pipelineInfo(
        {}, // flags
        stageInfo,
        m_fillLayout,
        nullptr, // basePipelineHandle
        -1 // basePipelineIndex
    );

    auto result = m_context.getDevice().createComputePipeline(nullptr, pipelineInfo);
    m_fillPipeline = result.value;

    m_context.getDevice().destroyShaderModule(shaderModule);
    LOG_DEBUG("Fill pipeline created");
}

void FieldRegistry::initializeField(const std::string& fieldName, const void* value) {
    if (!m_fillPipeline) {
        createFillPipeline();
    }

    LOG_DEBUG("Initializing field '{}'", fieldName);

    const auto& desc = getField(fieldName);
    float floatVal = 0.0f;
    if (value) {
        floatVal = *static_cast<const float*>(value);
    }

    vk::CommandBuffer cmd = m_context.beginSingleTimeCommands(m_computeCommandPool);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_fillPipeline);

    struct PushConstants {
        uint64_t bufferAddr;
        uint32_t elementCount;
        float value;
    } pc;

    pc.bufferAddr = static_cast<uint64_t>(desc.deviceAddress);
    pc.elementCount = m_activeVoxelCount;
    pc.value = floatVal;

    cmd.pushConstants<PushConstants>(m_fillLayout, vk::ShaderStageFlagBits::eCompute, 0, pc);

    uint32_t groupCount = (m_activeVoxelCount + 127) / 128;
    cmd.dispatch(groupCount, 1, 1);

    m_context.endSingleTimeCommands(cmd, m_computeCommandPool, m_context.getQueues().compute);
}

} // namespace field
