#include "script/LuaContext.hpp"
#include "script/SimulationEngine.hpp"
#include "core/Logger.hpp"

#include <sol/sol.hpp>

namespace script {

LuaContext::LuaContext() {
    LOG_INFO("Initializing Lua context");

    // Open standard libraries
    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table,
        sol::lib::io);

    LOG_DEBUG("Lua standard libraries loaded");
}

LuaContext::~LuaContext() {
    LOG_DEBUG("Lua context destroyed");
}

void LuaContext::bindFormats() {
    LOG_DEBUG("Binding Vulkan formats to Lua");

    // Create Format table
    sol::table formats = m_lua.create_table();

    // Scalar formats
    formats["R32F"] = "R32F";
    formats["R32I"] = "R32I";

    // Vector formats
    formats["R32G32F"] = "R32G32F";
    formats["R32G32I"] = "R32G32I";

    formats["R32G32B32F"] = "R32G32B32F";
    formats["R32G32B32I"] = "R32G32B32I";

    formats["R32G32B32A32F"] = "R32G32B32A32F";
    formats["R32G32B32A32I"] = "R32G32B32A32I";

    m_lua["Format"] = formats;
}

void LuaContext::bindSimulationEngine(SimulationEngine& engine) {
    LOG_DEBUG("Binding SimulationEngine to Lua");

    // Create usertype for SimulationEngine
    auto simType = m_lua.new_usertype<SimulationEngine>(
        "FluidEngine",
        sol::constructors<SimulationEngine(const SimulationEngine::Config&)>());

    // Bind methods
    simType["add_field"] = &SimulationEngine::addField;
    simType["add_stencil"] = [](SimulationEngine& self, const std::string& name, sol::table def) {
        stencil::StencilDefinition stencilDef;
        stencilDef.name = name;

        // Parse code
        if (def["code"].valid()) {
            stencilDef.code = def["code"].get<std::string>();
        }

        // Parse inputs
        if (def["inputs"].valid()) {
            sol::table inputs = def["inputs"];
            for (const auto& kv : inputs) {
                if (kv.second.is<std::string>()) {
                    stencilDef.inputs.push_back(kv.second.as<std::string>());
                }
            }
        }

        // Parse outputs
        if (def["outputs"].valid()) {
            sol::table outputs = def["outputs"];
            for (const auto& kv : outputs) {
                if (kv.second.is<std::string>()) {
                    stencilDef.outputs.push_back(kv.second.as<std::string>());
                }
            }
        }

        // Parse options
        if (def["requires_halos"].valid()) {
            stencilDef.requiresHalos = def["requires_halos"].get<bool>();
        }

        if (def["neighbor_radius"].valid()) {
            stencilDef.neighborRadius = def["neighbor_radius"].get<uint32_t>();
        }

        self.addStencil(stencilDef);
    };

    // Graph control methods (NEW)
    simType["build_graph"] = &SimulationEngine::buildDependencyGraph;
    simType["get_schedule"] = [this](SimulationEngine& self) -> sol::table {
        auto schedule = self.getExecutionSchedule();
        sol::table result = m_lua.create_table();
        for (size_t i = 0; i < schedule.size(); i++) {
            result[i + 1] = schedule[i];  // Lua is 1-indexed
        }
        return result;
    };
    simType["export_graph_dot"] = &SimulationEngine::exportGraphDOT;
    simType["set_execution_order"] = [](SimulationEngine& self, sol::table order) {
        std::vector<std::string> schedule;
        for (const auto& kv : order) {
            if (kv.second.is<std::string>()) {
                schedule.push_back(kv.second.as<std::string>());
            }
        }
        self.setExecutionOrder(schedule);
    };

    simType["step"] = &SimulationEngine::step;
    simType["run_frames"] = &SimulationEngine::runFrames;
    simType["get_gpu_count"] = &SimulationEngine::getGPUCount;
    simType["is_initialized"] = &SimulationEngine::isInitialized;

    LOG_DEBUG("SimulationEngine bindings complete");
}

void LuaContext::bindEngine(SimulationEngine& engine) {
    LOG_INFO("Binding engine to Lua VM");

    // Bind formats
    bindFormats();

    // Bind engine
    bindSimulationEngine(engine);

    // Create global engine instance
    m_lua["engine"] = &engine;

    LOG_INFO("Engine bindings complete");
}

void LuaContext::runScript(const std::filesystem::path& path) {
    LOG_INFO("Running Lua script: {}", path.string());

    if (!std::filesystem::exists(path)) {
        std::string error = "Script file not found: " + path.string();
        LOG_ERROR(error);
        throw std::runtime_error(error);
    }

    try {
        sol::protected_function_result result = m_lua.script_file(path.string());

        if (!result.valid()) {
            sol::error err = result;
            std::string errorMsg = err.what();
            LOG_ERROR("Lua script error: {}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        LOG_INFO("Script executed successfully");

    } catch (const std::exception& e) {
        LOG_ERROR("Exception running script: {}", e.what());
        throw;
    }
}

void LuaContext::runCode(const std::string& code) {
    LOG_DEBUG("Running Lua code snippet");

    try {
        sol::protected_function_result result = m_lua.script(code);

        if (!result.valid()) {
            sol::error err = result;
            std::string errorMsg = err.what();
            LOG_ERROR("Lua code error: {}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception running code: {}", e.what());
        throw;
    }
}

} // namespace script
