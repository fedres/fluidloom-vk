#pragma once

#include <string>
#include <vector>
#include <nanovdb/NanoVDB.h>
#include <nanovdb/GridHandle.h>
#include <nanovdb/HostBuffer.h>
#include <filesystem>
#include <memory>

namespace nanovdb_adapter {

/**
 * @brief Loads and validates NanoVDB grids from disk
 *
 * Supports loading .nvdb files with validation for grid types.
 */
class GridLoader {
public:
    /**
     * Load a NanoVDB grid from disk
     * @param path Path to .nvdb file
     * @param gridName Name of grid to load (empty = first grid)
     * @return Grid handle with host buffer
     */
    static nanovdb::GridHandle<nanovdb::HostBuffer>
    load(const std::filesystem::path& path, const std::string& gridName = "");

    /**
     * Validate grid type is supported
     * @param grid Grid to validate
     * @throws std::runtime_error if grid type is unsupported
     */
    static void validateGridType(const nanovdb::GridData* grid);
};

} // namespace nanovdb_adapter
