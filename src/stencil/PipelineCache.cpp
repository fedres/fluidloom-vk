#include "stencil/PipelineCache.hpp"
#include "core/Logger.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

namespace stencil {

PipelineCache::PipelineCache(const std::filesystem::path& cacheDir)
    : m_cacheDir(cacheDir) {
    // Create cache directory if it doesn't exist
    if (!std::filesystem::exists(m_cacheDir)) {
        std::filesystem::create_directories(m_cacheDir);
        LOG_INFO("Created shader cache directory: {}", m_cacheDir.string());
    } else {
        LOG_DEBUG("Using shader cache directory: {}", m_cacheDir.string());
    }
}

std::string PipelineCache::computeHash(const std::string& glslSource) const {
    // Compute SHA-256 hash
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(glslSource.data()),
           glslSource.size(), hash);

    // Convert to hex string
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(hash[i]);
    }

    return ss.str();
}

std::filesystem::path PipelineCache::getCachePath(
    const std::string& stencilName,
    const std::string& hash) const {
    // Format: stencilname_hash.spv
    std::string filename = stencilName + "_" + hash.substr(0, 8) + ".spv";
    return m_cacheDir / filename;
}

void PipelineCache::save(const std::string& stencilName,
                        const std::string& glslSource,
                        const std::vector<uint32_t>& spirv) {
    std::string hash = computeHash(glslSource);
    std::filesystem::path cachePath = getCachePath(stencilName, hash);

    try {
        std::ofstream file(cachePath, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to open cache file for writing: {}", cachePath.string());
            return;
        }

        file.write(reinterpret_cast<const char*>(spirv.data()),
                   spirv.size() * sizeof(uint32_t));
        file.close();

        LOG_DEBUG("Saved SPIR-V to cache: {} ({} bytes)",
                  cachePath.filename().string(), spirv.size() * 4);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving to cache: {}", e.what());
    }
}

std::vector<uint32_t> PipelineCache::load(const std::string& stencilName,
                                         const std::string& glslSource) {
    std::string hash = computeHash(glslSource);
    std::filesystem::path cachePath = getCachePath(stencilName, hash);

    if (!std::filesystem::exists(cachePath)) {
        LOG_DEBUG("Cache miss for stencil '{}'", stencilName);
        return {};
    }

    try {
        std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
        if (!file) {
            LOG_ERROR("Failed to open cache file: {}", cachePath.string());
            return {};
        }

        size_t fileSize = file.tellg();
        if (fileSize % 4 != 0) {
            LOG_ERROR("Invalid SPIR-V file size: {}", fileSize);
            return {};
        }

        std::vector<uint32_t> spirv(fileSize / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
        file.close();

        LOG_INFO("Cache hit for stencil '{}' ({} bytes)",
                 stencilName, fileSize);
        return spirv;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading from cache: {}", e.what());
        return {};
    }
}

bool PipelineCache::exists(const std::string& stencilName,
                           const std::string& glslSource) {
    std::string hash = computeHash(glslSource);
    std::filesystem::path cachePath = getCachePath(stencilName, hash);
    return std::filesystem::exists(cachePath);
}

void PipelineCache::clear() {
    LOG_INFO("Clearing shader cache: {}", m_cacheDir.string());
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(m_cacheDir)) {
            if (entry.path().extension() == ".spv") {
                std::filesystem::remove(entry.path());
                LOG_DEBUG("Removed: {}", entry.path().filename().string());
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception clearing cache: {}", e.what());
    }
}

} // namespace stencil
