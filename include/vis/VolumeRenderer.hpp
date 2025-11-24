#pragma once

#include "core/VulkanContext.hpp"
#include "core/MemoryAllocator.hpp"
#include "field/FieldRegistry.hpp"
#include "nanovdb_adapter/GpuGridManager.hpp"

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace vis {

// Forward declaration
struct Camera;

/**
 * @brief Real-time volume visualization of fluid simulation
 *
 * Renders volumetric field data using GPU raymarching. Leverages GPU-resident
 * data to avoid expensive CPU readbacks. Supports multiple visualization modes
 * (density, velocity magnitude, custom fields).
 */
class VolumeRenderer {
public:
    /**
     * @brief Visualization configuration
     */
    struct Config {
        uint32_t width = 1920;
        uint32_t height = 1080;
        float stepSize = 0.5f;                      // Ray marching step size
        float densityScale = 1.0f;                  // Density multiplier
        float opacityScale = 1.0f;                  // Opacity multiplier
        glm::vec3 lightDir = glm::vec3(0, 1, 0);   // Directional light direction
        bool showGrid = false;                      // Visualize grid boundaries
    };

    /**
     * @brief Camera specification for rendering
     */
    struct Camera {
        glm::vec3 position;
        glm::vec3 target;
        glm::vec3 up;
        float fov = 45.0f;
        float aspectRatio = 16.0f / 9.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };

    /**
     * @brief Initialize volume renderer
     * @param ctx Vulkan context
     * @param renderPass Vulkan render pass (for swapchain attachment)
     * @param config Visualization settings
     */
    VolumeRenderer(core::VulkanContext& ctx,
                   vk::RenderPass renderPass,
                   const Config& config = {});

    /**
     * @brief Cleanup renderer resources
     */
    ~VolumeRenderer();

    /**
     * @brief Update descriptor bindings for current fields
     * @param registry Field registry containing fields to visualize
     * @param gridMgr GPU grid manager with active grid
     * @param fieldName Name of field to visualize (default: first field)
     */
    void updateDescriptors(const field::FieldRegistry& registry,
                          const nanovdb_adapter::GpuGridManager& gridMgr,
                          const std::string& fieldName = "");

    /**
     * @brief Render volume to framebuffer
     * @param cmd Command buffer for recording render commands
     * @param camera Camera parameters
     * @param framebuffer Target framebuffer (swapchain image)
     */
    void render(vk::CommandBuffer cmd,
               const Camera& camera,
               vk::Framebuffer framebuffer);

    /**
     * @brief Update visualization settings
     * @param config New configuration
     */
    void setConfig(const Config& config);

    /**
     * @brief Get current configuration
     */
    const Config& getConfig() const { return m_config; }

    /**
     * @brief Get available fields for visualization
     */
    std::vector<std::string> getAvailableFields() const { return m_availableFields; }

    /**
     * @brief Set active visualization field
     */
    void setVisualizationField(const std::string& fieldName);

private:
    core::VulkanContext& m_context;
    vk::RenderPass m_renderPass;
    Config m_config;

    std::vector<std::string> m_availableFields;
    std::string m_currentField;

    // Graphics pipeline resources
    vk::Pipeline m_pipeline;
    vk::PipelineLayout m_pipelineLayout;
    vk::DescriptorSet m_descriptorSet;
    vk::DescriptorSetLayout m_descriptorSetLayout;
    vk::DescriptorPool m_descriptorPool;

    // Camera uniform buffer
    core::MemoryAllocator::Buffer m_cameraUBO;

    // Render configuration push constants buffer
    core::MemoryAllocator::Buffer m_configBuffer;

    // Framebuffer size
    uint32_t m_width;
    uint32_t m_height;

    /**
     * @brief Create graphics pipeline for volume rendering
     */
    void createPipeline();

    /**
     * @brief Create descriptor set layout and pool
     */
    void createDescriptorLayout();

    /**
     * @brief Destroy pipeline and descriptor resources
     */
    void destroyPipeline();

    /**
     * @brief Update camera uniform buffer
     */
    void updateCamera(const Camera& camera);

    /**
     * @brief Bind field and grid descriptors
     */
    void bindFieldDescriptors(const field::FieldRegistry& registry,
                             const nanovdb_adapter::GpuGridManager& gridMgr);
};

} // namespace vis
