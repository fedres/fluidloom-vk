#include "stencil/StencilParser.hpp"
#include "core/Logger.hpp"

#include <sol/sol.hpp>
#include <stdexcept>

namespace stencil {

StencilParser::StencilParser(const field::FieldRegistry& fieldRegistry)
    : m_fieldRegistry(fieldRegistry) {
    LOG_DEBUG("StencilParser initialized");
}

std::vector<std::string> StencilParser::extractStringArray(
    const sol::table& table, const std::string& key) {
    std::vector<std::string> result;
    
    sol::optional<sol::table> arrayOpt = table[key];
    if (!arrayOpt) {
        return result;
    }
    
    sol::table array = arrayOpt.value();
    for (const auto& pair : array) {
        if (pair.second.is<std::string>()) {
            result.push_back(pair.second.as<std::string>());
        }
    }
    
    return result;
}

void StencilParser::validateFields(const StencilDefinition& def) {
    // Validate input fields
    for (const auto& fieldName : def.inputs) {
        if (!m_fieldRegistry.hasField(fieldName)) {
            throw std::runtime_error("Input field not found in registry: " + fieldName);
        }
    }
    
    // Validate output fields
    for (const auto& fieldName : def.outputs) {
        if (!m_fieldRegistry.hasField(fieldName)) {
            throw std::runtime_error("Output field not found in registry: " + fieldName);
        }
    }
    
    LOG_DEBUG("Field validation passed for stencil '{}'", def.name);
}

StencilDefinition StencilParser::parse(const sol::table& luaTable) {
    StencilDefinition def;
    
    // Extract name (required)
    sol::optional<std::string> nameOpt = luaTable["name"];
    if (!nameOpt) {
        throw std::runtime_error("Stencil definition missing required 'name' field");
    }
    def.name = nameOpt.value();
    
    LOG_DEBUG("Parsing stencil: '{}'", def.name);
    
    // Extract inputs (required)
    def.inputs = extractStringArray(luaTable, "inputs");
    if (def.inputs.empty()) {
        LOG_WARN("Stencil '{}' has no input fields", def.name);
    }
    
    // Extract outputs (required)
    def.outputs = extractStringArray(luaTable, "outputs");
    if (def.outputs.empty()) {
        throw std::runtime_error("Stencil '" + def.name + "' must have at least one output field");
    }
    
    // Extract code (required)
    sol::optional<std::string> codeOpt = luaTable["code"];
    if (!codeOpt) {
        throw std::runtime_error("Stencil '" + def.name + "' missing required 'code' field");
    }
    def.code = codeOpt.value();
    
    // Extract optional neighbor_radius
    sol::optional<uint32_t> radiusOpt = luaTable["neighbor_radius"];
    def.neighborRadius = radiusOpt.value_or(0);
    
    // Set flags based on neighbor radius
    def.requiresNeighbors = (def.neighborRadius > 0);
    def.requiresHalos = def.requiresNeighbors;
    
    // Validate against field registry
    validateFields(def);
    
    LOG_INFO("Parsed stencil '{}': {} inputs, {} outputs, neighbor_radius={}",
             def.name, def.inputs.size(), def.outputs.size(), def.neighborRadius);
    
    return def;
}

} // namespace stencil
