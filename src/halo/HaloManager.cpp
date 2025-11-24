#include "halo/HaloManager.hpp"
#include "core/Logger.hpp"

#include <stdexcept>

namespace halo {

HaloManager::HaloManager(const core::VulkanContext& context,
                        core::MemoryAllocator& allocator,
                        const std::vector<domain::SubDomain>& domains)
    : m_context(context), m_allocator(allocator), m_domains(domains) {
    LOG_INFO("Initializing HaloManager for {} GPUs, halo thickness: {}",
             domains.size(), m_haloThickness);
}

HaloManager::~HaloManager() {
    // Cleanup halos
    for (auto& [fieldName, gpuHalos] : m_fieldHalos) {
        for (auto& haloSet : gpuHalos) {
            for (int face = 0; face < 6; face++) {
                m_allocator.destroyBuffer(haloSet.localHalos[face]);
                m_allocator.destroyBuffer(haloSet.remoteHalos[face]);
            }
        }
    }

    // Cleanup semaphores
    for (auto sem : m_haloSemaphores) {
        m_context.getDevice().destroySemaphore(sem);
    }

    LOG_DEBUG("HaloManager destroyed");
}

HaloBufferSet::FaceDimensions HaloManager::calculateFaceDimensions(
    const domain::SubDomain& domain,
    uint32_t face) {
    HaloBufferSet::FaceDimensions dims;
    dims.thickness = m_haloThickness;

    const auto& bounds = domain.bounds;
    uint32_t dimX = bounds.max()[0] - bounds.min()[0] + 1;
    uint32_t dimY = bounds.max()[1] - bounds.min()[1] + 1;
    uint32_t dimZ = bounds.max()[2] - bounds.min()[2] + 1;

    // Face ordering: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z
    switch (face) {
        case 0: // -X face
        case 1: // +X face
            dims.width = dimY;
            dims.height = dimZ;
            break;
        case 2: // -Y face
        case 3: // +Y face
            dims.width = dimX;
            dims.height = dimZ;
            break;
        case 4: // -Z face
        case 5: // +Z face
            dims.width = dimX;
            dims.height = dimY;
            break;
        default:
            throw std::runtime_error("Invalid face index");
    }

    return dims;
}

void HaloManager::allocateFieldHalos(const std::string& fieldName,
                                    const field::FieldDesc& fieldDesc,
                                    uint32_t gpuIndex) {
    LOG_INFO("Allocating halos for field '{}' on GPU {}", fieldName, gpuIndex);

    if (gpuIndex >= m_domains.size()) {
        throw std::runtime_error("GPU index out of range");
    }

    // Get or create halo storage for this field
    if (m_fieldHalos.find(fieldName) == m_fieldHalos.end()) {
        m_fieldHalos[fieldName] = std::vector<HaloBufferSet>(m_domains.size());
    }

    HaloBufferSet& haloSet = m_fieldHalos[fieldName][gpuIndex];
    const auto& domain = m_domains[gpuIndex];

    // Allocate buffers for each face
    for (int face = 0; face < 6; face++) {
        auto faceDims = calculateFaceDimensions(domain, face);
        haloSet.faceDims[face] = faceDims;

        uint32_t haloVoxelCount = faceDims.thickness * faceDims.width * faceDims.height;
        haloSet.haloVoxelCounts[face] = haloVoxelCount;

        vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(haloVoxelCount) *
                                   fieldDesc.elementSize;

        // Local halo: stores data received from neighbors
        LOG_DEBUG("  Face {}: {} voxels ({} bytes)", face, haloVoxelCount, bufferSize);

        haloSet.localHalos[face] = m_allocator.createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eShaderDeviceAddress);

        // Remote halo: stores data to be sent to neighbors
        // Use host-to-GPU memory for easier packing
        haloSet.remoteHalos[face] = m_allocator.createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferSrc |
            vk::BufferUsageFlagBits::eShaderDeviceAddress);

        // Initialize sync values
        haloSet.writeValues[face] = 0;
        haloSet.readValues[face] = 0;
    }

    LOG_DEBUG("Halo buffers allocated for field '{}'", fieldName);
}

void HaloManager::createHaloSemaphores() {
    LOG_INFO("Creating timeline semaphores for halo synchronization");

    uint32_t gpuCount = static_cast<uint32_t>(m_domains.size());

    // Create one semaphore per (src, dst) GPU pair
    m_haloSemaphores.resize(gpuCount * gpuCount);

    vk::SemaphoreTypeCreateInfo timelineCreateInfo(
        vk::SemaphoreType::eTimeline,
        0 // initialValue
    );

    vk::SemaphoreCreateInfo createInfo;
    createInfo.setPNext(&timelineCreateInfo);

    for (uint32_t src = 0; src < gpuCount; src++) {
        for (uint32_t dst = 0; dst < gpuCount; dst++) {
            if (src == dst) {
                continue;  // No self-semaphores
            }

            try {
                m_haloSemaphores[src * gpuCount + dst] =
                    m_context.getDevice().createSemaphore(createInfo);
                LOG_DEBUG("Created semaphore for GPU {} -> GPU {}", src, dst);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to create semaphore: {}", e.what());
                throw;
            }
        }
    }

    LOG_INFO("Timeline semaphores created ({} total)", gpuCount * gpuCount);
}

HaloBufferSet& HaloManager::getHaloBufferSet(const std::string& fieldName,
                                             uint32_t gpuIndex) {
    auto it = m_fieldHalos.find(fieldName);
    if (it == m_fieldHalos.end()) {
        throw std::runtime_error("Halo buffers not allocated for field: " + fieldName);
    }

    if (gpuIndex >= it->second.size()) {
        throw std::runtime_error("GPU index out of range");
    }

    return it->second[gpuIndex];
}

vk::Semaphore HaloManager::getHaloSemaphore(uint32_t srcGpu, uint32_t dstGpu) {
    uint32_t gpuCount = static_cast<uint32_t>(m_domains.size());

    if (srcGpu >= gpuCount || dstGpu >= gpuCount) {
        throw std::runtime_error("GPU index out of range");
    }

    vk::Semaphore sem = m_haloSemaphores[srcGpu * gpuCount + dstGpu];
    if (!sem) {
        throw std::runtime_error("Semaphore not created for GPU " +
                                std::to_string(srcGpu) + " -> " +
                                std::to_string(dstGpu));
    }

    return sem;
}

} // namespace halo
