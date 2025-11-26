// src/main.cpp
// Main executable for FluidLoom

#define VK_NO_PROTOTYPES

#include "script/LuaContext.hpp"
#include "script/SimulationEngine.hpp"
#include "core/Logger.hpp"

/*
  ______ _       _     _ _                     
 |  ____| |     (_)   | | |                    
 | |__  | |_   _ _  __| | |     ___   ___  _ __ ___ 
 |  __| | | | | | |/ _` | |    / _ \ / _ \| '_ ` _ \
 | |    | | |_| | | (_| | |___| (_) | (_) | | | | | |
 |_|    |_|\__,_|_|\__,_|______\___/ \___/|_| |_| |_|
                                                     
  FluidLoom - GPU-Accelerated Fluid Simulation Engine
  
  Author: zombie aka Karthik Thyagarajan
*/

#include <iostream>
#include <filesystem>

// Define Vulkan dynamic dispatcher storage
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <script.lua>" << std::endl;
            std::cerr << "Example: " << argv[0] << " tests/integration/minimal_test.lua" << std::endl;
            return 1;
        }

        std::string scriptPath = argv[1];
        
        if (!std::filesystem::exists(scriptPath)) {
            std::cerr << "Error: Script file not found: " << scriptPath << std::endl;
            return 1;
        }

        std::cout << "FluidLoom - GPU-Accelerated Fluid Simulation Engine\n";
        std::cout << "===================================================\n\n";
        std::cout << "Loading script: " << scriptPath << "\n\n";

        // Create Lua context
        script::LuaContext lua;
        
        // Create simulation engine with default config
        script::SimulationEngine::Config config;
        config.gpuCount = 1;
        config.haloThickness = 2;
        
        script::SimulationEngine engine(config);
        
        // Bind engine to Lua
        lua.bindEngine(engine);
        
        // Run the script
        lua.runScript(scriptPath);
        
        std::cout << "\n✓ Script execution complete!\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Error: " << e.what() << std::endl;
        return 1;
    }
}
