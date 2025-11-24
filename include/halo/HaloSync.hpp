#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace halo {

/**
 * @brief Timeline semaphore synchronization for halo exchange
 *
 * Coordinates multi-GPU halo operations using timeline semaphores.
 * Ensures proper ordering:
 * 1. Pack halos (compute shader extracts boundary data)
 * 2. Transfer halos (copy between GPUs)
 * 3. Unpack halos (write boundary data into local arrays)
 * 4. Compute (use halo data in stencils)
 */
class HaloSync {
public:
    /**
     * @brief Halo exchange operation tracking
     */
    struct HaloExchange {
        uint32_t srcGpu = 0;              // Source GPU index
        uint32_t dstGpu = 0;              // Destination GPU index
        uint32_t srcFace = 0;             // Source face (0-5)
        uint32_t dstFace = 0;             // Destination face (opposite)
        uint64_t signalValue = 0;         // Semaphore value to signal after transfer
        uint64_t waitValue = 0;           // Semaphore value to wait for before use
    };

    /**
     * Initialize halo synchronization
     * @param gpuCount Number of GPUs in simulation
     */
    explicit HaloSync(uint32_t gpuCount);

    /**
     * Record halo pack operation (extract boundary voxels)
     * @param cmd Command buffer
     * @param gpu GPU index
     * @param sem Semaphore to signal when done
     * @param value Semaphore value to signal
     */
    void recordHaloPack(vk::CommandBuffer cmd,
                       uint32_t gpu,
                       vk::Semaphore sem,
                       uint64_t value);

    /**
     * Record halo transfer operation (copy between GPUs)
     * @param cmd Command buffer
     * @param srcGpu Source GPU
     * @param dstGpu Destination GPU
     * @param waitSem Semaphore to wait on (pack complete)
     * @param waitValue Semaphore value to wait for
     * @param signalSem Semaphore to signal (transfer complete)
     * @param signalValue Semaphore value to signal
     */
    void recordHaloTransfer(vk::CommandBuffer cmd,
                           uint32_t srcGpu,
                           uint32_t dstGpu,
                           vk::Semaphore waitSem,
                           uint64_t waitValue,
                           vk::Semaphore signalSem,
                           uint64_t signalValue);

    /**
     * Record halo unpack operation (write received data)
     * @param cmd Command buffer
     * @param gpu GPU index
     * @param waitSem Semaphore to wait on (transfer complete)
     * @param waitValue Semaphore value to wait for
     */
    void recordHaloUnpack(vk::CommandBuffer cmd,
                         uint32_t gpu,
                         vk::Semaphore waitSem,
                         uint64_t waitValue);

    /**
     * Create synchronization commands for a timestep
     * @param exchanges Vector of halo exchanges to perform
     * @return Vector of command buffers (one per GPU)
     */
    std::vector<vk::CommandBuffer> generateSyncCommands(
        const std::vector<HaloExchange>& exchanges);

private:
    uint32_t m_gpuCount;

    // Barrier structures
    vk::MemoryBarrier createMemoryBarrier();
};

} // namespace halo
