#include "stencil/ShaderGenerator.hpp"
#include "core/Logger.hpp"

#include <sstream>
#include <regex>

namespace stencil {

ShaderGenerator::ShaderGenerator(const field::FieldRegistry& fieldRegistry)
    : m_fieldRegistry(fieldRegistry) {
    LOG_DEBUG("ShaderGenerator initialized");
}

std::string ShaderGenerator::generateHeader() {
    return R"(
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_separate_shader_objects : require

layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;

)";
}

std::string ShaderGenerator::generateBufferReferences() {
    std::stringstream ss;
    ss << "// --- Field Buffer References ---\n";

    for (const auto& [name, desc] : m_fieldRegistry.getFields()) {
        std::string glslType = desc.getGLSLType();
        ss << "layout(buffer_reference, scalar) buffer " << name << "_Buffer { "
           << glslType << " data[]; };\n";
    }

    ss << "\n";
    return ss.str();
}

std::string ShaderGenerator::generatePushConstants() {
    std::stringstream ss;
    ss << "// --- Push Constants ---\n";
    ss << "layout(push_constant, std430) uniform PC {\n";
    ss << "    uint64_t gridAddr;           // NanoVDB grid device address\n";
    ss << "    uint64_t bdaTableAddr;       // Field BDA table address\n";
    ss << "    uint32_t activeVoxelCount;   // Total active voxels\n";
    ss << "    uint32_t neighborRadius;     // For accessing neighbor voxels\n";

    // Add individual field addresses
    uint32_t fieldIndex = 0;
    for (const auto& [name, desc] : m_fieldRegistry.getFields()) {
        ss << "    uint64_t field_" << name << "_addr;  // Field '" << name << "' address\n";
        fieldIndex++;
    }

    ss << "} pc;\n\n";
    return ss.str();
}

std::string ShaderGenerator::generateHelperFunctions() {
    std::stringstream ss;
    
    ss << R"(
// --- NanoVDB Accessor Helper Functions ---

// NanoVDB grid structure accessor
// The grid is passed via pc.gridAddr as a buffer device address
layout(buffer_reference, scalar) buffer NanoVDBGrid {
    // Simplified NanoVDB grid header structure
    // In production, this would include full NanoVDB tree nodes
    uint64_t tree;           // Offset to tree structure
    uint32_t activeVoxelCount;
    uint32_t gridClass;
    uvec3 gridDims;          // Bounding box dimensions
    ivec3 gridMin;           // Bounding box min corner
};

layout(buffer_reference, scalar) buffer VoxelCoordMap {
    // Maps linear active voxel index to IJK coordinate
    ivec3 coords[];
};

layout(buffer_reference, scalar) buffer CoordToIndexMap {
    // NanoVDB accessor table for IJK -> active index
    uint indices[];
};

// Get 3D coordinate from linear active voxel index
ivec3 getVoxelCoord(uint linearIdx) {
    // Access NanoVDB grid structure
    NanoVDBGrid grid = NanoVDBGrid(pc.gridAddr);
    
    // Method 1: If we have a precomputed mapping table
    // This would be uploaded alongside the grid
    // VoxelCoordMap coordMap = VoxelCoordMap(grid.coordMapAddr);
    // return coordMap.coords[linearIdx];
    
    // Method 2: Traverse NanoVDB tree structure
    // This is more complex but doesn't require extra storage
    // Would use nanovdb::ReadAccessor in GLSL equivalent
    
    // For now, using simplified Morton-based mapping
    // This assumes active voxels are in Morton order
    // Real implementation would query the actual NanoVDB tree
    
    // Decode Morton code (Z-order curve)
    uint mortonCode = linearIdx;  // Simplified assumption
    
    uint x = 0, y = 0, z = 0;
    for (uint i = 0; i < 21; i++) {  // 21 bits per dimension for 2^21 = 2M range
        x |= ((mortonCode >> (3*i + 0)) & 1) << i;
        y |= ((mortonCode >> (3*i + 1)) & 1) << i;
        z |= ((mortonCode >> (3*i + 2)) & 1) << i;
    }
    
    return ivec3(int(x), int(y), int(z)) + grid.gridMin;
}

// Check if a coordinate is within active voxels
bool isActiveVoxel(ivec3 coord) {
    NanoVDBGrid grid = NanoVDBGrid(pc.gridAddr);
    
    // Real NanoVDB implementation would:
    // 1. Traverse tree from root
    // 2. Check if coordinate is in active leaf node
    // 3. Check if voxel is active within leaf
    
    // Simplified: bounds check
    ivec3 gridMax = grid.gridMin + ivec3(grid.gridDims);
    return all(greaterThanEqual(coord, grid.gridMin)) && 
           all(lessThan(coord, gridMax));
}

// Get linear index from 3D coordinate
// Returns ~0u if coordinate is not active
uint coordToLinearIdx(ivec3 coord) {
    if (!isActiveVoxel(coord)) {
        return ~0u;  // Invalid index
    }
    
    NanoVDBGrid grid = NanoVDBGrid(pc.gridAddr);
    
    // Real NanoVDB implementation:
    // 1. Traverse tree to find leaf node containing coord
    // 2. Get leaf's active voxel offset
    // 3. Find voxel bit in leaf's active mask
    // 4. Count bits before this one to get local index
    // 5. Add leaf offset to get global linear index
    
    // Simplified Morton encoding
    ivec3 localCoord = coord - grid.gridMin;
    uint x = uint(localCoord.x);
    uint y = uint(localCoord.y);
    uint z = uint(localCoord.z);
    
    uint mortonCode = 0;
    for (uint i = 0; i < 21; i++) {
        mortonCode |= ((x >> i) & 1) << (3*i + 0);
        mortonCode |= ((y >> i) & 1) << (3*i + 1);
        mortonCode |= ((z >> i) & 1) << (3*i + 2);
    }
    
    return mortonCode;
}

// Read from neighbor voxel by offset
float readNeighborFloat(uint64_t fieldAddr, uint linearIdx, ivec3 offset) {
    ivec3 coord = getVoxelCoord(linearIdx);
    ivec3 neighborCoord = coord + offset;
    
    uint neighborIdx = coordToLinearIdx(neighborCoord);
    
    // Check if neighbor is valid
    if (neighborIdx == ~0u) {
        return 0.0;  // Outside active voxels or domain
    }
    
    // Read from field buffer
    layout(buffer_reference, scalar) buffer FieldBuf { float data[]; };
    return FieldBuf(fieldAddr).data[neighborIdx];
}

// Read vec3 from neighbor
vec3 readNeighborVec3(uint64_t fieldAddr, uint linearIdx, ivec3 offset) {
    ivec3 coord = getVoxelCoord(linearIdx);
    ivec3 neighborCoord = coord + offset;
    
    uint neighborIdx = coordToLinearIdx(neighborCoord);
    
    if (neighborIdx == ~0u) {
        return vec3(0.0);
    }
    
    layout(buffer_reference, scalar) buffer Vec3Buf { vec3 data[]; };
    return Vec3Buf(fieldAddr).data[neighborIdx];
}

// Standard 6-neighbor stencil helpers (±X, ±Y, ±Z)
float readNeighbor_XPlus(uint64_t fieldAddr, uint linearIdx) {
    return readNeighborFloat(fieldAddr, linearIdx, ivec3(1, 0, 0));
}

float readNeighbor_XMinus(uint64_t fieldAddr, uint linearIdx) {
    return readNeighborFloat(fieldAddr, linearIdx, ivec3(-1, 0, 0));
}

float readNeighbor_YPlus(uint64_t fieldAddr, uint linearIdx) {
    return readNeighborFloat(fieldAddr, linearIdx, ivec3(0, 1, 0));
}

float readNeighbor_YMinus(uint64_t fieldAddr, uint linearIdx) {
    return readNeighborFloat(fieldAddr, linearIdx, ivec3(0, -1, 0));
}

float readNeighbor_ZPlus(uint64_t fieldAddr, uint linearIdx) {
    return readNeighborFloat(fieldAddr, linearIdx, ivec3(0, 0, 1));
}

float readNeighbor_ZMinus(uint64_t fieldAddr, uint linearIdx) {
    return readNeighborFloat(fieldAddr, linearIdx, ivec3(0, 0, -1));
}

)";
    
    return ss.str();
}

std::string ShaderGenerator::sanitizeUserCode(const std::string& code) {
    std::string processed = code;
    
    // Replace Read_field(idx) macros with proper buffer access
    // Pattern: Read_<fieldname>(idx)
    std::regex readPattern(R"(Read_(\w+)\s*\(\s*(\w+)\s*\))");
    processed = std::regex_replace(processed, readPattern, 
        "$1_Buffer(pc.field_$1_addr).data[$2]");
    
    // Replace Write_field(idx, value) macros with proper buffer access
    // Pattern: Write_<fieldname>(idx, value)
    std::regex writePattern(R"(Write_(\w+)\s*\(\s*(\w+)\s*,\s*([^)]+)\))");
    processed = std::regex_replace(processed, writePattern,
        "$1_Buffer(pc.field_$1_addr).data[$2] = $3");
    
    // NEW: Replace ReadNeighbor_field(idx, offset) macros
    // Pattern: ReadNeighbor_<fieldname>(idx, ivec3(x, y, z))
    // This allows: float neighbor = ReadNeighbor_density(linearIdx, ivec3(1, 0, 0));
    std::regex neighborReadPattern(R"(ReadNeighbor_(\w+)\s*\(\s*(\w+)\s*,\s*([^)]+)\))");
    processed = std::regex_replace(processed, neighborReadPattern,
        "readNeighborFloat(pc.field_$1_addr, $2, $3)");
    
    // NEW: Replace ReadNeighborVec3_field(idx, offset) for vector fields
    std::regex neighborVec3Pattern(R"(ReadNeighborVec3_(\w+)\s*\(\s*(\w+)\s*,\s*([^)]+)\))");
    processed = std::regex_replace(processed, neighborVec3Pattern,
        "readNeighborVec3(pc.field_$1_addr, $2, $3)");
    
    LOG_DEBUG("User code transformed: {} -> {} bytes", code.size(), processed.size());
    return processed;
}

std::string ShaderGenerator::generateMainFunction(const StencilDefinition& stencil) {
    std::stringstream ss;

    ss << "// --- Main Computation ---\n";
    ss << "void main() {\n";
    ss << "    uint linearIdx = gl_GlobalInvocationID.x;\n";
    ss << "    if (linearIdx >= pc.activeVoxelCount) return;\n";
    ss << "\n";

    // Inject user code
    std::string userCode = sanitizeUserCode(stencil.code);
    ss << "    // --- User Stencil Code ---\n";
    ss << "    " << userCode << "\n";
    ss << "    // --- End User Code ---\n";

    ss << "}\n";
    return ss.str();
}

std::string ShaderGenerator::generateComputeShader(const StencilDefinition& stencil) {
    LOG_INFO("Generating compute shader for stencil: '{}'", stencil.name);

    std::stringstream ss;

    // Header
    ss << generateHeader();

    // Buffer references
    ss << generateBufferReferences();

    // Push constants
    ss << generatePushConstants();

    // Helper functions
    ss << generateHelperFunctions();

    // Main function with user code
    ss << generateMainFunction(stencil);

    std::string source = ss.str();
    LOG_DEBUG("Shader generation complete ({} bytes)", source.size());

    return source;
}

} // namespace stencil
