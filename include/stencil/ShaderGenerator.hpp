#pragma once

#include "stencil/StencilDefinition.hpp"
#include "field/FieldRegistry.hpp"

#include <string>
#include <vector>

namespace stencil {

/**
 * @brief Generates GLSL compute shaders from stencil definitions
 *
 * Converts user-provided stencil specifications into valid GLSL 4.6
 * compute shader source with proper buffer references and field access.
 */
class ShaderGenerator {
public:
    /**
     * Initialize shader generator
     * @param fieldRegistry Field registry for buffer layout info
     */
    explicit ShaderGenerator(const field::FieldRegistry& fieldRegistry);

    /**
     * Generate GLSL compute shader source from stencil definition
     * @param stencil Stencil definition
     * @return Valid GLSL source code
     */
    std::string generateComputeShader(const StencilDefinition& stencil);

private:
    const field::FieldRegistry& m_fieldRegistry;

    /**
     * Generate shader header with extensions and version
     */
    std::string generateHeader();

    /**
     * Generate buffer reference declarations for all fields
     */
    std::string generateBufferReferences();

    /**
     * Generate push constant structure
     */
    std::string generatePushConstants();

    /**
     * Generate main function with user code injection
     * @param stencil Stencil definition
     */
    std::string generateMainFunction(const StencilDefinition& stencil);

    /**
     * Sanitize user code for safety
     * @param code User-provided code
     * @return Sanitized code
     */
    std::string sanitizeUserCode(const std::string& code);

    /**
     * Generate helper functions for field access
     */
    std::string generateHelperFunctions();
};

} // namespace stencil
