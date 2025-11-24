#include "field/FieldRegistry.hpp"
#include "core/VulkanContext.hpp"
#include "core/Logger.hpp"

#include <stdexcept>
#include <sstream>

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
    // Stub: Fill pipeline creation would go here
    // This requires shader compilation which is deferred to Module 6
    LOG_DEBUG("createFillPipeline stub (to be implemented in Module 6)");
}

void FieldRegistry::initializeField(const std::string& fieldName, const void* value) {
    // Stub: Initialize with compute shader
    // This requires the fill pipeline which is created lazily in Module 6
    LOG_DEBUG("Initializing field '{}' (stub - to be implemented in Module 6)", fieldName);
}

} // namespace field
