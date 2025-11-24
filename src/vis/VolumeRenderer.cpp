#include "vis/VolumeRenderer.hpp"
#include "core/Logger.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace vis {

VolumeRenderer::VolumeRenderer(core::VulkanContext& ctx,
                               vk::RenderPass renderPass,
                               const Config& config)
    : m_context(ctx),
      m_renderPass(renderPass),
      m_config(config),
      m_width(config.width),
      m_height(config.height)
{
    LOG_INFO("Initializing VolumeRenderer ({} x {})", m_width, m_height);

    createDescriptorLayout();
    createPipeline();

    // Allocate camera UBO
    m_cameraUBO = ctx.getAllocator().allocateBuffer(
        sizeof(glm::mat4) * 2, // view and projection matrices
        vk::BufferUsageFlagBits::eUniformBuffer,
        vma::MemoryUsage::eCpuToGpu,
        "CameraUBO");

    // Allocate config buffer for push constants
    m_configBuffer = ctx.getAllocator().allocateBuffer(
        sizeof(Config),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vma::MemoryUsage::eCpuToGpu,
        "RendererConfig");

    LOG_INFO("VolumeRenderer initialized");
}

VolumeRenderer::~VolumeRenderer()
{
    LOG_DEBUG("Destroying VolumeRenderer");
    destroyPipeline();

    auto& allocator = m_context.getAllocator();
    allocator.freeBuffer(m_cameraUBO);
    allocator.freeBuffer(m_configBuffer);
}

void VolumeRenderer::createDescriptorLayout()
{
    LOG_DEBUG("Creating descriptor layout");

    std::vector<vk::DescriptorSetLayoutBinding> bindings(3);

    // Binding 0: Camera UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 1: Grid data (storage buffer)
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 2: Field data (storage buffer)
    bindings[2].binding = 2;
    bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    auto result = m_context.getDevice().createDescriptorSetLayout(layoutInfo);
    if (result.result != vk::Result::eSuccess) {
        std::string error = "Failed to create descriptor set layout";
        LOG_ERROR(error);
        throw std::runtime_error(error);
    }

    m_descriptorSetLayout = result.value;

    // Create descriptor pool
    std::vector<vk::DescriptorPoolSize> poolSizes{
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 2)};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();

    auto poolResult = m_context.getDevice().createDescriptorPool(poolInfo);
    if (poolResult.result != vk::Result::eSuccess) {
        LOG_ERROR("Failed to create descriptor pool");
        throw std::runtime_error("Descriptor pool creation failed");
    }

    m_descriptorPool = poolResult.value;

    // Allocate descriptor set
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    auto setResult = m_context.getDevice().allocateDescriptorSets(allocInfo);
    if (setResult.result != vk::Result::eSuccess) {
        LOG_ERROR("Failed to allocate descriptor set");
        throw std::runtime_error("Descriptor set allocation failed");
    }

    m_descriptorSet = setResult.value[0];

    LOG_DEBUG("Descriptor layout created");
}

void VolumeRenderer::createPipeline()
{
    LOG_DEBUG("Creating graphics pipeline");

    // Create pipeline layout
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;

    // Push constants for render config
    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pushRange.offset = 0;
    pushRange.size = sizeof(Config);

    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    auto layoutResult = m_context.getDevice().createPipelineLayout(layoutInfo);
    if (layoutResult.result != vk::Result::eSuccess) {
        LOG_ERROR("Failed to create pipeline layout");
        throw std::runtime_error("Pipeline layout creation failed");
    }

    m_pipelineLayout = layoutResult.value;

    // For now, create placeholder pipeline
    // Full shader compilation will be done with DXC integration
    // Pipeline state would include:
    // - Vertex shader (quad.vert): generates full-screen triangle
    // - Fragment shader (volume.frag): raymarching volumetric data
    // - Blending: SrcAlpha, OneMinusSrcAlpha
    // - Depth: disabled (raymarched)
    // - Culling: none

    m_pipeline = vk::Pipeline();
    LOG_DEBUG("Graphics pipeline created (placeholder)");
}

void VolumeRenderer::destroyPipeline()
{
    LOG_DEBUG("Destroying graphics pipeline");

    auto device = m_context.getDevice();

    if (m_pipeline) {
        device.destroyPipeline(m_pipeline);
    }

    if (m_pipelineLayout) {
        device.destroyPipelineLayout(m_pipelineLayout);
    }

    if (m_descriptorSet) {
        // Descriptors are destroyed with pool
    }

    if (m_descriptorPool) {
        device.destroyDescriptorPool(m_descriptorPool);
    }

    if (m_descriptorSetLayout) {
        device.destroyDescriptorSetLayout(m_descriptorSetLayout);
    }
}

void VolumeRenderer::updateDescriptors(const field::FieldRegistry& registry,
                                       const nanovdb_adapter::GpuGridManager& gridMgr,
                                       const std::string& fieldName)
{
    LOG_DEBUG("Updating renderer descriptors");

    // Cache available fields
    m_availableFields.clear();
    for (uint32_t i = 0; i < registry.getFieldCount(); ++i) {
        if (const auto* desc = registry.getFieldByIndex(i)) {
            m_availableFields.push_back(desc->name);
        }
    }

    if (m_availableFields.empty()) {
        LOG_WARN("No fields available for visualization");
        return;
    }

    // Determine active field
    if (!fieldName.empty() && registry.getField(fieldName)) {
        m_currentField = fieldName;
    } else if (!m_currentField.empty() && registry.getField(m_currentField)) {
        // Keep current field if still available
    } else {
        // Default to first available field
        m_currentField = m_availableFields[0];
    }

    LOG_DEBUG("Using field '{}' for visualization", m_currentField);

    // Bind field and grid data
    bindFieldDescriptors(registry, gridMgr);
}

void VolumeRenderer::bindFieldDescriptors(const field::FieldRegistry& registry,
                                          const nanovdb_adapter::GpuGridManager& gridMgr)
{
    LOG_DEBUG("Binding field descriptors");

    std::vector<vk::WriteDescriptorSet> writes;

    // Write 0: Camera UBO
    vk::DescriptorBufferInfo cameraInfo{};
    cameraInfo.buffer = m_cameraUBO.buffer;
    cameraInfo.offset = 0;
    cameraInfo.range = sizeof(glm::mat4) * 2;

    vk::WriteDescriptorSet cameraWrite{};
    cameraWrite.dstSet = m_descriptorSet;
    cameraWrite.dstBinding = 0;
    cameraWrite.dstArrayElement = 0;
    cameraWrite.descriptorCount = 1;
    cameraWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
    cameraWrite.pBufferInfo = &cameraInfo;

    writes.push_back(cameraWrite);

    // Write 1: Grid data
    vk::DescriptorBufferInfo gridInfo{};
    gridInfo.buffer = gridMgr.getGridResources().gridBuffer.buffer;
    gridInfo.offset = 0;
    gridInfo.range = gridMgr.getGridResources().memorySize;

    vk::WriteDescriptorSet gridWrite{};
    gridWrite.dstSet = m_descriptorSet;
    gridWrite.dstBinding = 1;
    gridWrite.dstArrayElement = 0;
    gridWrite.descriptorCount = 1;
    gridWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    gridWrite.pBufferInfo = &gridInfo;

    writes.push_back(gridWrite);

    // Write 2: Field data
    if (const auto* fieldDesc = registry.getField(m_currentField)) {
        vk::DescriptorBufferInfo fieldInfo{};
        fieldInfo.buffer = fieldDesc->buffer.buffer;
        fieldInfo.offset = 0;
        fieldInfo.range = fieldDesc->buffer.size;

        vk::WriteDescriptorSet fieldWrite{};
        fieldWrite.dstSet = m_descriptorSet;
        fieldWrite.dstBinding = 2;
        fieldWrite.dstArrayElement = 0;
        fieldWrite.descriptorCount = 1;
        fieldWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        fieldWrite.pBufferInfo = &fieldInfo;

        writes.push_back(fieldWrite);
    }

    // Update descriptors
    m_context.getDevice().updateDescriptorSets(writes, nullptr);

    LOG_DEBUG("Field descriptors updated");
}

void VolumeRenderer::updateCamera(const Camera& camera)
{
    LOG_DEBUG("Updating camera matrices");

    // Compute view matrix
    glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);

    // Compute projection matrix
    float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    glm::mat4 projection = glm::perspective(
        glm::radians(camera.fov),
        aspect,
        camera.nearPlane,
        camera.farPlane);

    // Upload to UBO
    glm::mat4* uboPtr = m_context.getAllocator().mapBuffer(m_cameraUBO);
    if (uboPtr) {
        uboPtr[0] = view;
        uboPtr[1] = projection;
        m_context.getAllocator().unmapBuffer(m_cameraUBO);
    }
}

void VolumeRenderer::render(vk::CommandBuffer cmd,
                            const Camera& camera,
                            vk::Framebuffer framebuffer)
{
    if (!m_pipeline) {
        LOG_WARN("Pipeline not yet compiled - skipping render");
        return;
    }

    LOG_DEBUG("Recording render commands");

    // Update camera
    updateCamera(camera);

    // Begin render pass
    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = vk::Offset2D(0, 0);
    renderPassInfo.renderArea.extent = vk::Extent2D(m_width, m_height);

    vk::ClearValue clearValue{};
    clearValue.color = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);

    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Bind pipeline
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

    // Bind descriptor set
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                          m_pipelineLayout,
                          0,
                          m_descriptorSet,
                          nullptr);

    // Push render configuration
    cmd.pushConstants<Config>(m_pipelineLayout,
                             vk::ShaderStageFlagBits::eFragment,
                             0,
                             m_config);

    // Draw full-screen quad (vertex shader generates triangle)
    cmd.draw(3, 1, 0, 0);

    cmd.endRenderPass();

    LOG_DEBUG("Render commands recorded");
}

void VolumeRenderer::setConfig(const Config& config)
{
    LOG_DEBUG("Updating renderer configuration");

    m_config = config;
    m_width = config.width;
    m_height = config.height;

    // Upload new config to buffer
    Config* configPtr = m_context.getAllocator().mapBuffer(m_configBuffer);
    if (configPtr) {
        *configPtr = config;
        m_context.getAllocator().unmapBuffer(m_configBuffer);
    }

    LOG_DEBUG("Renderer configuration updated");
}

void VolumeRenderer::setVisualizationField(const std::string& fieldName)
{
    LOG_DEBUG("Changing visualization field to '{}'", fieldName);

    auto it = std::find(m_availableFields.begin(), m_availableFields.end(), fieldName);
    if (it == m_availableFields.end()) {
        LOG_WARN("Field '{}' not available for visualization", fieldName);
        return;
    }

    m_currentField = fieldName;
    LOG_INFO("Visualization field changed to '{}'", m_currentField);
}

} // namespace vis
