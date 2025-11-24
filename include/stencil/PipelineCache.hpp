#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

namespace stencil {

/**
 * @brief Disk-based SPIR-V pipeline cache
 *
 * Caches compiled SPIR-V bytecode to avoid recompilation.
 * Uses SHA-256 hash of GLSL source as cache key.
 */
class PipelineCache {
public:
    /**
     * Initialize pipeline cache
     * @param cacheDir Directory to store cached SPIR-V files
     */
    explicit PipelineCache(const std::filesystem::path& cacheDir);

    /**
     * Save SPIR-V to cache
     * @param stencilName Unique stencil name
     * @param glslSource GLSL source code (for hash computation)
     * @param spirv Compiled SPIR-V bytecode
     */
    void save(const std::string& stencilName,
              const std::string& glslSource,
              const std::vector<uint32_t>& spirv);

    /**
     * Load SPIR-V from cache
     * @param stencilName Unique stencil name
     * @param glslSource GLSL source code (for hash verification)
     * @return SPIR-V bytecode, or empty vector if cache miss
     */
    std::vector<uint32_t> load(const std::string& stencilName,
                              const std::string& glslSource);

    /**
     * Check if cached SPIR-V exists and is valid
     * @param stencilName Unique stencil name
     * @param glslSource GLSL source code (for hash verification)
     */
    bool exists(const std::string& stencilName,
                const std::string& glslSource);

    /**
     * Clear all cached SPIR-V files
     */
    void clear();

    /**
     * Get cache directory path
     */
    const std::filesystem::path& getCacheDir() const { return m_cacheDir; }

private:
    std::filesystem::path m_cacheDir;

    /**
     * Compute SHA-256 hash of GLSL source
     * @param glslSource GLSL source code
     * @return Hex-encoded hash string
     */
    std::string computeHash(const std::string& glslSource) const;

    /**
     * Get cache file path for a stencil
     * @param stencilName Unique stencil name
     * @param hash Hash of GLSL source
     */
    std::filesystem::path getCachePath(const std::string& stencilName,
                                      const std::string& hash) const;
};

} // namespace stencil
