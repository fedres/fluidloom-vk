#include "nanovdb_adapter/GridLoader.hpp"
#include "core/Logger.hpp"

#include <nanovdb/io/IO.h>
#include <stdexcept>

namespace nanovdb_adapter {

nanovdb::GridHandle<nanovdb::HostBuffer>
GridLoader::load(const std::filesystem::path& path, const std::string& gridName) {
    LOG_INFO("Loading NanoVDB grid from: {}", path.string());

    if (!std::filesystem::exists(path)) {
        std::string msg = "NanoVDB file not found: " + path.string();
        LOG_ERROR(msg);
        throw std::runtime_error(msg);
    }

    try {
        nanovdb::GridHandle<nanovdb::HostBuffer> handle;

        if (gridName.empty()) {
            // Load first grid
            LOG_DEBUG("Loading first grid from file");
            handle = nanovdb::io::readGrid(path.string());
        } else {
            // Load specific grid by name
            LOG_DEBUG("Loading grid '{}' from file", gridName);
            handle = nanovdb::io::readGrid(path.string(), gridName);
        }

        auto* grid = handle.gridData(0);
        LOG_CHECK(grid != nullptr, "Failed to load grid from file");

        validateGridType(grid);

        auto bbox = grid->indexBBox();
        LOG_INFO("Grid loaded successfully. Bounds: [{},{},{}] to [{},{},{}], Type: {}",
                 bbox.min()[0], bbox.min()[1], bbox.min()[2],
                 bbox.max()[0], bbox.max()[1], bbox.max()[2],
                 grid->mGridType == nanovdb::GridType::Float ? "Float" : "Other");

        return handle;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception while loading grid: {}", e.what());
        throw;
    }
}

void GridLoader::validateGridType(const nanovdb::GridData* grid) {
    LOG_CHECK(grid != nullptr, "Grid is null");

    nanovdb::GridType type = grid->mGridType;

    // Supported types: Float (density/temperature), Vec3f (velocity)
    if (type != nanovdb::GridType::Float &&
        type != nanovdb::GridType::Vec3f) {
        std::string msg = "Unsupported grid type: " + std::to_string(static_cast<int>(type));
        LOG_ERROR(msg);
        throw std::runtime_error(msg);
    }

    LOG_DEBUG("Grid type validation passed");
}

} // namespace nanovdb_adapter
