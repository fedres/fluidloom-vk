# Module 12: Build System & CI/CD

## Overview
This module manages the build configuration and continuous integration pipelines.

## Build System
- **Tool**: CMake 3.28+.
- **Structure**:
    - Root `CMakeLists.txt` defines options and subdirectories.
    - `src/CMakeLists.txt` gathers sources.
    - `external/CMakeLists.txt` manages dependencies.

## CI/CD Pipeline
- **Platform**: GitHub Actions.
- **Workflows**:
    - `build.yml`: Compiles code on Linux/Windows.
    - `test.yml`: Runs unit tests.
    - `lint.yml`: Runs Clang-Tidy and CppCheck.

## Detailed Implementation Plan

### Phase 1: CMake Configuration
**Goal**: Robust build script with Vulkan C++ bindings.

1.  **File**: `CMakeLists.txt`
    -   **Step 1.1**: Project Setup
        ```cmake
        cmake_minimum_required(VERSION 3.28)
        project(FluidEngine VERSION 0.1 LANGUAGES CXX)

        # Require C++20 for Vulkan-HPP features
        set(CMAKE_CXX_STANDARD 20)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)

        # Add compile options
        if(MSVC)
            add_compile_options(/W4 /WX)
        else()
            add_compile_options(-Wall -Wextra -Wpedantic)
        endif()
        ```
    -   **Step 1.2**: Dependencies
        ```cmake
        find_package(Vulkan REQUIRED COMPONENTS glslc)
        find_package(spdlog CONFIG REQUIRED)
        find_package(Catch2 CONFIG REQUIRED)
        find_package(glm CONFIG REQUIRED)

        # Vulkan-HPP is header-only, included with Vulkan SDK
        # vk-bootstrap and VMA via vcpkg or submodules
        find_package(vk-bootstrap CONFIG REQUIRED)
        find_package(VulkanMemoryAllocator CONFIG REQUIRED)

        # NanoVDB (assuming submodule or manual install)
        add_subdirectory(external/nanovdb)
        ```
    -   **Step 1.3**: Shader Compilation (Custom Command)
        ```cmake
        function(compile_shader SHADER_SOURCE)
            get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
            set(SPV_FILE "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

            add_custom_command(
                OUTPUT ${SPV_FILE}
                # Use glslc from Vulkan SDK
                COMMAND ${Vulkan_GLSLC_EXECUTABLE}
                    -fshader-stage=comp
                    -std=460
                    -O
                    -g  # Debug symbols for RenderDoc
                    ${SHADER_SOURCE}
                    -o ${SPV_FILE}
                DEPENDS ${SHADER_SOURCE}
                COMMENT "Compiling shader ${SHADER_NAME}"
            )

            # Add to target
            target_sources(FluidEngineCore PRIVATE ${SPV_FILE})
        endfunction()

        # Glob all shaders
        file(GLOB SHADER_SOURCES "${CMAKE_SOURCE_DIR}/shaders/*.comp")
        foreach(SHADER ${SHADER_SOURCES})
            compile_shader(${SHADER})
        endforeach()
        ```
    -   **Step 1.4**: Main Library Target
        ```cmake
        add_library(FluidEngineCore STATIC
            src/core/VulkanContext.cpp
            src/core/MemoryAllocator.cpp
            src/core/Logger.cpp
            src/nanovdb_adapter/GridLoader.cpp
            src/nanovdb_adapter/GpuGridManager.cpp
            src/field/FieldRegistry.cpp
            src/domain/DomainSplitter.cpp
            src/halo/HaloManager.cpp
            src/stencil/StencilCompiler.cpp
            src/graph/DependencyGraph.cpp
            src/graph/GraphExecutor.cpp
            src/script/LuaContext.cpp
            # ... other sources
        )

        target_link_libraries(FluidEngineCore PUBLIC
            Vulkan::Vulkan
            vk-bootstrap::vk-bootstrap
            GPUOpen::VulkanMemoryAllocator
            spdlog::spdlog
            glm::glm
            nanovdb
            sol2::sol2
        )

        target_include_directories(FluidEngineCore PUBLIC
            ${CMAKE_SOURCE_DIR}/src
            ${Vulkan_INCLUDE_DIRS}
        )

        # Define VK_NO_PROTOTYPES for volk
        target_compile_definitions(FluidEngineCore PUBLIC
            VK_NO_PROTOTYPES
            VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
        )
        ```

### Phase 2: GitHub Actions
**Goal**: Auto-build on push.

1.  **File**: `.github/workflows/build.yml`
    -   **Step 2.1**: Trigger
        `on: [push, pull_request]`
    -   **Step 2.2**: Job
        -   Runs-on: `ubuntu-latest`.
        -   Steps:
            -   Checkout.
            -   Install Vulkan SDK (`lunarg/setup-vulkan-sdk`).
            -   Configure CMake.
            -   Build.
            -   Run Tests (`ctest`).

## Exposed Interfaces

### CMake Targets
-   `FluidEngineCore` (Static Lib)
-   `FluidSim` (Executable)
-   `UnitTests` (Executable)
