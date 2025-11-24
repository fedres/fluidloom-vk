#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace stencil {

/**
 * @brief User-defined stencil operation specification
 *
 * Represents a single compute kernel that operates on fields.
 * Can be defined at runtime via user scripts.
 */
struct StencilDefinition {
    std::string name;                          // Unique stencil name
    std::vector<std::string> inputs;           // Input field names
    std::vector<std::string> outputs;          // Output field names
    std::string code;                          // User-written shader code snippet
    uint32_t neighborRadius = 0;               // Neighbor voxel access radius (0 = no neighbors)
    bool requiresHalos = false;                // Whether this stencil needs halo data
    bool requiresNeighbors = false;            // Whether this stencil reads neighbor voxels
};

/**
 * @brief Compiled stencil pipeline and metadata
 */
struct CompiledStencil {
    StencilDefinition definition;
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    std::vector<uint32_t> spirvCode;           // SPIR-V bytecode
    std::string glslSource;                    // Generated GLSL source (for debugging)
};

} // namespace stencil
