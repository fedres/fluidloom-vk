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
    return R"(
// --- Helper Functions ---

// Read from a field by voxel index
float ReadField(uint64_t fieldAddr, uint idx) {
    return vec4BitsToFloat(floatBitsToUint(uint32_t(idx))); // Placeholder
}

// Write to a field by voxel index
void WriteField(uint64_t fieldAddr, uint idx, float val) {
    // Placeholder - actual implementation depends on field format
}

)";
}

std::string ShaderGenerator::sanitizeUserCode(const std::string& code) {
    // Basic sanitization: remove potentially dangerous patterns
    std::string sanitized = code;

    // Remove or escape semicolons at weird places
    // This is a placeholder - real implementation would be more sophisticated

    LOG_DEBUG("User code sanitized");
    return sanitized;
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
