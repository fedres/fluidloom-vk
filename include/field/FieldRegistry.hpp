#pragma once

#include "core/VulkanContext.hpp"
#include "core/MemoryAllocator.hpp"

#include <vulkan/vulkan.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace field {

/**
 * @brief Field descriptor with GPU buffer and metadata
 */
struct FieldDesc {
    std::string name;
    vk::Format format;                          // vk::Format::eR32Sfloat, eR32G32B32Sfloat, etc.
    uint32_t elementSize;                       // sizeof(float), sizeof(glm::vec3)
    core::MemoryAllocator::Buffer buffer;       // GPU buffer
    uint32_t descriptorIndex;                   // Index into BDA table
    vk::DeviceAddress deviceAddress;            // For bindless shader access

    /**
     * Get GLSL type string for this field
     */
    std::string getGLSLType() const {
        switch (format) {
            case vk::Format::eR32Sfloat:
                return "float";
            case vk::Format::eR32G32Sfloat:
                return "vec2";
            case vk::Format::eR32G32B32Sfloat:
                return "vec3";
            case vk::Format::eR32G32B32A32Sfloat:
                return "vec4";
            case vk::Format::eR32Sint:
                return "int";
            case vk::Format::eR32G32Sint:
                return "ivec2";
            case vk::Format::eR32G32B32Sint:
                return "ivec3";
            case vk::Format::eR32G32B32A32Sint:
                return "ivec4";
            default:
                return "float"; // Fallback
        }
    }
};

/**
 * @brief Dynamic field registry with bindless descriptor table
 *
 * Manages Structure of Arrays (SoA) layout where each field is a separate buffer.
 * Maintains a GPU-side BDA (Buffer Device Address) table for bindless access.
 */
class FieldRegistry {
public:
    static constexpr uint32_t MAX_FIELDS = 256;

    /**
     * Initialize field registry
     * @param context Vulkan context
     * @param allocator Memory allocator
     * @param activeVoxelCount Number of active voxels in the simulation
     */
    FieldRegistry(core::VulkanContext& context,
                 core::MemoryAllocator& allocator,
                 uint32_t activeVoxelCount);

    ~FieldRegistry();

    /**
     * Register a new field
     * @param name Unique field name
     * @param format Vulkan format (e.g., eR32Sfloat)
     * @param initialValue Pointer to initial value (single value, will be splatted)
     * @return Reference to field descriptor
     * @throws std::runtime_error if field already exists or MAX_FIELDS exceeded
     */
    const FieldDesc& registerField(const std::string& name,
                                   vk::Format format,
                                   const void* initialValue = nullptr);

    /**
     * Query field by name
     * @throws std::runtime_error if field not found
     */
    const FieldDesc& getField(const std::string& name) const;

    /**
     * Check if field exists
     */
    bool hasField(const std::string& name) const;

    /**
     * Get GPU address of BDA table (for push constants)
     */
    vk::DeviceAddress getBDATableAddress() const;

    /**
     * Generate GLSL header with buffer references and accessor macros
     * @return GLSL code string
     */
    std::string generateGLSLHeader() const;

    /**
     * Get all registered fields
     */
    const std::unordered_map<std::string, FieldDesc>& getFields() const {
        return m_fields;
    }

    /**
     * Get field count
     */
    uint32_t getFieldCount() const { return static_cast<uint32_t>(m_fields.size()); }

    /**
     * Get active voxel count this registry was configured for
     */
    uint32_t getActiveVoxelCount() const { return m_activeVoxelCount; }

private:
    core::VulkanContext& m_context;
    core::MemoryAllocator& m_allocator;
    uint32_t m_activeVoxelCount;

    // Field storage: name -> descriptor
    std::unordered_map<std::string, FieldDesc> m_fields;

    // BDA table: GPU buffer containing device addresses of all field buffers
    core::MemoryAllocator::Buffer m_bdaTableBuffer;
    uint64_t* m_bdaTableMapped = nullptr;

    // Next available descriptor index
    uint32_t m_nextDescriptorIndex = 0;

    // Fill compute shader for initialization
    vk::Pipeline m_fillPipeline;
    vk::PipelineLayout m_fillLayout;
    vk::CommandPool m_computeCommandPool;

    /**
     * Create and compile the fill shader pipeline
     */
    void createFillPipeline();

    /**
     * Initialize field buffer to a constant value
     */
    void initializeField(const std::string& fieldName, const void* value);
};

} // namespace field
