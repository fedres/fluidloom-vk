#include "vis/VolumeRenderer.hpp"
#include "core/Logger.hpp"

#include <fstream>
#include <cstdlib>
#include <cstdio>
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

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pushRange.offset = 0;
    pushRange.size = sizeof(Config);

    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    auto layoutResult = m_context.getDevice().createPipelineLayout(layoutInfo);
    if (layoutResult.result != vk::Result::eSuccess) {
        throw std::runtime_error("Pipeline layout creation failed");
    }

    m_pipelineLayout = layoutResult.value;

    // --- Compile Vertex Shader ---
    std::string vertSource = R"(
#version 460
layout(location = 0) out vec2 outUV;
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outUV = uv;
}
)";
    std::string uuid = "quad_" + std::to_string(std::rand());
    std::string glslFile = "/tmp/" + uuid + ".vert";
    std::string spvFile = "/tmp/" + uuid + ".spv";
    {
        std::ofstream out(glslFile);
        out << vertSource;
    }
    std::system(("/opt/homebrew/bin/glslc -fshader-stage=vertex -o " + spvFile + " " + glslFile).c_str());
    std::remove(glslFile.c_str());

    std::vector<uint32_t> vertSpirv;
    {
        std::ifstream in(spvFile, std::ios::binary | std::ios::ate);
        if (in) {
            size_t size = in.tellg();
            vertSpirv.resize(size/4);
            in.seekg(0);
            in.read((char*)vertSpirv.data(), size);
        }
    }
    std::remove(spvFile.c_str());

    vk::ShaderModuleCreateInfo vertModuleInfo{
        .codeSize = vertSpirv.size() * 4,
        .pCode = vertSpirv.data()
    };
    vk::ShaderModule vertModule = m_context.getDevice().createShaderModule(vertModuleInfo);


    // --- Compile Fragment Shader ---
    std::string fragSource = R"(
#version 460
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    float density;
    float threshold;
    uint width;
    uint height;
} pc;

layout(std140, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;

// Assuming field data is bound here
layout(std430, binding = 2) buffer FieldData {
    float data[];
};

// Simple ray-box intersection
vec2 intersectBox(vec3 orig, vec3 dir) {
    vec3 boxMin = vec3(0.0);
    vec3 boxMax = vec3(1.0); // Normalized volume coordinates
    vec3 invDir = 1.0 / dir;
    vec3 tmin = (boxMin - orig) * invDir;
    vec3 tmax = (boxMax - orig) * invDir;
    vec3 t1 = min(tmin, tmax);
    vec3 t2 = max(tmin, tmax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    return vec2(tNear, tFar);
}

void main() {
    // Ray generation
    vec4 ndc = vec4(inUV * 2.0 - 1.0, -1.0, 1.0);
    vec4 viewSpace = inverse(camera.proj) * ndc;
    viewSpace /= viewSpace.w;
    vec4 worldSpace = inverse(camera.view) * viewSpace;
    vec3 rayOrigin = (inverse(camera.view) * vec4(0,0,0,1)).xyz;
    vec3 rayDir = normalize(worldSpace.xyz - rayOrigin);

    // Intersect volume
    vec2 t = intersectBox(rayOrigin, rayDir);
    if (t.x > t.y) {
        outColor = vec4(0.0); // Miss
        return;
    }

    t.x = max(t.x, 0.0);
    vec3 pos = rayOrigin + rayDir * t.x;
    float stepSize = 0.01;
    vec4 color = vec4(0.0);

    // Raymarch
    for (float dist = t.x; dist < t.y; dist += stepSize) {
        if (color.a >= 0.99) break;

        // Sample field (placeholder mapping from pos to index)
        // In real impl, use NanoVDB accessor or 3D texture
        // Here we just map 0..1 to linear index for demo
        uint idx = uint(pos.x * 100 + pos.y * 100 * 100 + pos.z * 100 * 100 * 100) % 1000000; 
        // Safety check
        if (idx < 1000000) { 
             float val = data[idx];
             
             if (val > pc.threshold) {
                 float alpha = pc.density * stepSize;
                 vec3 srcColor = vec3(val); // Simple transfer function
                 color.rgb += (1.0 - color.a) * alpha * srcColor;
                 color.a += (1.0 - color.a) * alpha;
             }
        }

        pos += rayDir * stepSize;
    }

    outColor = color;
}
)";
    uuid = "volume_" + std::to_string(std::rand());
    glslFile = "/tmp/" + uuid + ".frag";
    spvFile = "/tmp/" + uuid + ".spv";
    {
        std::ofstream out(glslFile);
        out << fragSource;
    }
    std::system(("/opt/homebrew/bin/glslc -fshader-stage=fragment -o " + spvFile + " " + glslFile).c_str());
    std::remove(glslFile.c_str());

    std::vector<uint32_t> fragSpirv;
    {
        std::ifstream in(spvFile, std::ios::binary | std::ios::ate);
        if (in) {
            size_t size = in.tellg();
            fragSpirv.resize(size/4);
            in.seekg(0);
            in.read((char*)fragSpirv.data(), size);
        }
    }
    std::remove(spvFile.c_str());

    vk::ShaderModuleCreateInfo fragModuleInfo{
        .codeSize = fragSpirv.size() * 4,
        .pCode = fragSpirv.data()
    };
    vk::ShaderModule fragModule = m_context.getDevice().createShaderModule(fragModuleInfo);

    // Pipeline creation
    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        {
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = vertModule,
            .pName = "main"
        },
        {
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = fragModule,
            .pName = "main"
        }
    };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::Viewport viewport{};
    viewport.width = (float)m_width;
    viewport.height = (float)m_height;
    viewport.maxDepth = 1.0f;
    vk::Rect2D scissor{};
    scissor.extent = vk::Extent2D(m_width, m_height);
    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    auto result = m_context.getDevice().createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }
    m_pipeline = result.value;

    m_context.getDevice().destroyShaderModule(vertModule);
    m_context.getDevice().destroyShaderModule(fragModule);

    LOG_DEBUG("Graphics pipeline created");
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
