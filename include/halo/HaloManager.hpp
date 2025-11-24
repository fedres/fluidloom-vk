#pragma once

#include "core/VulkanContext.hpp"
#include "core/MemoryAllocator.hpp"
#include "domain/DomainSplitter.hpp"
#include "field/FieldRegistry.hpp"

#include <vulkan/vulkan.hpp>
#include <vector>
#include <array>
#include <cstdint>

namespace halo {

/**
 * @brief Per-face halo buffer storage
 *
 * Manages halo layers (boundary voxels) for a single field across all 6 faces.
 */
struct HaloBufferSet {
    // Local halos: data read from neighbor GPUs
    std::array<core::MemoryAllocator::Buffer, 6> localHalos;

    // Remote halos: data to be sent to neighbor GPUs
    std::array<core::MemoryAllocator::Buffer, 6> remoteHalos;

    // Timeline semaphore values for synchronization
    std::array<uint64_t, 6> writeValues;  // Last completed halo write per face
    std::array<uint64_t, 6> readValues;   // Last consumed halo read per face

    // Halo dimensions per face
    struct FaceDimensions {
        uint32_t thickness = 0;
        uint32_t width = 0;   // Y or Z dimension
        uint32_t height = 0;  // Z or Y dimension
    };
    std::array<FaceDimensions, 6> faceDims;

    // Total voxels in each halo (thickness × width × height)
    std::array<uint32_t, 6> haloVoxelCounts;
};

/**
 * @brief Manages halo exchange for multi-GPU simulations
 *
 * Handles:
 * - Halo buffer allocation per field and domain
 * - Packing halo data (extract boundary voxels from main buffer)
 * - Unpacking halo data (write received boundary voxels)
 * - Timeline semaphore synchronization between GPUs
 */
class HaloManager {
public:
    /**
     * Initialize halo manager for multi-GPU simulation
     * @param context Vulkan context
     * @param allocator Memory allocator
     * @param domains Domain decomposition result
     */
    HaloManager(const core::VulkanContext& context,
               core::MemoryAllocator& allocator,
               const std::vector<domain::SubDomain>& domains);

    ~HaloManager();

    /**
     * Allocate halo buffers for a specific field
     * @param fieldName Field name
     * @param fieldDesc Field descriptor with format and element size
     * @param gpuIndex GPU to allocate halos for
     */
    void allocateFieldHalos(const std::string& fieldName,
                           const field::FieldDesc& fieldDesc,
                           uint32_t gpuIndex);

    /**
     * Create timeline semaphores for halo synchronization
     * Creates one semaphore per neighbor per GPU
     */
    void createHaloSemaphores();

    /**
     * Get halo buffer set for a field on a specific GPU
     * @throws std::runtime_error if field halos not allocated
     */
    HaloBufferSet& getHaloBufferSet(const std::string& fieldName, uint32_t gpuIndex);

    /**
     * Get timeline semaphore for inter-GPU synchronization
     * @param srcGpu Source GPU index
     * @param dstGpu Destination GPU index
     */
    vk::Semaphore getHaloSemaphore(uint32_t srcGpu, uint32_t dstGpu);

    /**
     * Get halo thickness (constant across all domains)
     */
    uint32_t getHaloThickness() const { return m_haloThickness; }

    /**
     * Get number of GPUs
     */
    uint32_t getGPUCount() const { return static_cast<uint32_t>(m_domains.size()); }

private:
    const core::VulkanContext& m_context;
    core::MemoryAllocator& m_allocator;
    const std::vector<domain::SubDomain>& m_domains;

    uint32_t m_haloThickness = 2;  // Standard: 2 voxels for 2nd-order stencils

    // Halo buffers per field per GPU
    // Structure: m_fieldHalos[fieldName][gpuIndex] -> HaloBufferSet
    std::unordered_map<std::string, std::vector<HaloBufferSet>> m_fieldHalos;

    // Timeline semaphores for inter-GPU synchronization
    // Structure: m_haloSemaphores[srcGpu * gpuCount + dstGpu] -> vk::Semaphore
    std::vector<vk::Semaphore> m_haloSemaphores;

    /**
     * Calculate halo dimensions for a specific domain and face
     */
    HaloBufferSet::FaceDimensions calculateFaceDimensions(
        const domain::SubDomain& domain,
        uint32_t face);
};

} // namespace halo
