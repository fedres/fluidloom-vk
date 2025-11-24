#pragma once

#include "stencil/StencilDefinition.hpp"
#include "field/FieldRegistry.hpp"

// Forward declare sol::table to avoid including sol.hpp in header
namespace sol {
    class table;
}

namespace stencil {

/**
 * @brief Parser for Lua-based stencil DSL
 *
 * Converts Lua table definitions into C++ StencilDefinition structs.
 * Validates field names against FieldRegistry.
 */
class StencilParser {
public:
    /**
     * Initialize parser
     * @param fieldRegistry Field registry for validation
     */
    explicit StencilParser(const field::FieldRegistry& fieldRegistry);

    /**
     * Parse Lua table into StencilDefinition
     * @param luaTable Lua table with stencil definition
     * @return Parsed and validated stencil definition
     * @throws std::runtime_error if validation fails
     *
     * Expected Lua format:
     * ```lua
     * {
     *   name = "advect_density",
     *   inputs = {"density", "velocity"},
     *   outputs = {"density_new"},
     *   code = [[
     *     vec3 vel = Read_velocity(idx);
     *     float d = Read_density(idx);
     *     -- ... user code ...
     *     Write_density_new(idx, d);
     *   ]],
     *   neighbor_radius = 1  -- optional, default 0
     * }
     * ```
     */
    StencilDefinition parse(const sol::table& luaTable);

private:
    const field::FieldRegistry& m_fieldRegistry;

    /**
     * Validate that all field names exist in registry
     */
    void validateFields(const StencilDefinition& def);

    /**
     * Extract string vector from Lua table
     */
    std::vector<std::string> extractStringArray(const sol::table& table, const std::string& key);
};

} // namespace stencil
