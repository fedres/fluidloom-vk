#pragma once

#include <filesystem>
#include <memory>
#include <sol/sol.hpp>

namespace script {

// Forward declaration
class SimulationEngine;

/**
 * @brief Lua scripting runtime and bindings
 *
 * Provides user-facing API for simulation control via Lua scripts.
 * Enables dynamic field/stencil definition without engine recompilation.
 */
class LuaContext {
public:
    /**
     * Initialize Lua context with standard libraries
     */
    LuaContext();

    /**
     * Cleanup Lua state
     */
    ~LuaContext();

    /**
     * Bind C++ engine to Lua VM
     * @param engine Simulation engine instance
     */
    void bindEngine(SimulationEngine& engine);

    /**
     * Execute a Lua script file
     * @param path Path to .lua script
     * @throws std::runtime_error on script errors
     */
    void runScript(const std::filesystem::path& path);

    /**
     * Execute Lua code directly
     * @param code Lua source code
     * @throws std::runtime_error on execution errors
     */
    void runCode(const std::string& code);

    /**
     * Get the underlying Lua state (for advanced usage)
     */
    sol::state& getLuaState() { return m_lua; }

private:
    sol::state m_lua;

    /**
     * Bind field format enums to Lua
     */
    void bindFormats();

    /**
     * Bind simulation engine methods and properties
     */
    void bindSimulationEngine(SimulationEngine& engine);
};

} // namespace script
