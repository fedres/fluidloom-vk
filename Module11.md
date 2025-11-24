# Module 11: Testing & Verification

## Overview
This module ensures the correctness and performance of the engine through automated testing.

## Strategy

### 1. Unit Testing
- **Framework**: Catch2.
- **Scope**:
    - Individual classes (`DomainSplitter`, `FieldRegistry`).
    - Math helpers.
    - Grid loading.

### 2. Integration Testing
- **Scope**:
    - Full pipeline runs (Load -> Sim -> Output).
    - Multi-GPU halo exchange verification.
    - DSL compilation and execution.

### 3. Performance Benchmarking
- **Framework**: Google Benchmark (or custom).
- **Metrics**:
    - Timestep duration (ms).
    - Memory bandwidth (GB/s).
    - Voxel updates per second.

## Detailed Implementation Plan

### Phase 1: Test Infrastructure
**Goal**: Setup Catch2 environment.

1.  **File**: `tests/main.cpp`
    ```cpp
    #define CATCH_CONFIG_MAIN
    #include <catch2/catch.hpp>
    ```
2.  **Fixture**: `tests/VulkanFixture.hpp`
    -   Setup `VulkanContext` in constructor.
    -   Teardown in destructor.
    -   Provide `runCompute(shader, ...)` helper.

### Phase 2: Core Tests
**Goal**: Verify basic components.

1.  **Test**: `tests/TestGridLoader.cpp`
    -   Load "sphere.nvdb".
    -   `REQUIRE(grid.activeVoxelCount() > 0)`.
2.  **Test**: `tests/TestFieldRegistry.cpp`
    -   `reg.registerField("density", ...)`
    -   `REQUIRE_THROWS(reg.registerField("density", ...))` (Duplicate).

### Phase 3: Integration Tests
**Goal**: Verify pipeline.

1.  **Test**: `tests/TestSimulation.cpp`
    ```cpp
    TEST_CASE_METHOD(VulkanFixture, "Advection Test") {
        // 1. Setup
        FluidEngine engine(ctx);
        engine.addField("density", VK_FORMAT_R32_SFLOAT, 1.0f);
        
        // 2. Define Stencil (Double Value)
        engine.addStencil("double", {
            .inputs = {"density"},
            .outputs = {"density"},
            .code = "Write(density, idx, Read(density, idx) * 2.0);"
        });
        
        // 3. Run
        engine.run(0.016f);
        
        // 4. Verify
        auto data = engine.downloadField("density");
        REQUIRE(data[0] == Approx(2.0f));
    }
    ```

## Exposed Interfaces

### Class: `VulkanFixture`
```cpp
class VulkanFixture {
public:
    VulkanContext ctx;
    MemoryAllocator alloc;

    VulkanFixture() : alloc(ctx) {
        // Initialize with validation layers
        ctx.init(true);

        // Create command pool for test operations
        cmdPool = ctx.createCommandPool(
            ctx.getQueues().computeFamily,
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer
        );
    }

    ~VulkanFixture() {
        ctx.getDevice().destroyCommandPool(cmdPool);
        ctx.cleanup();
    }

    // Helper: run a simple compute shader and return results
    template<typename T>
    std::vector<T> runCompute(const std::string& shaderPath,
                             const std::vector<T>& inputData,
                             size_t outputSize) {
        // Implementation details for test helper
        // ... allocate buffers, compile shader, dispatch, read back
    }

private:
    vk::CommandPool cmdPool;
};

### Additional Test Helpers
```cpp
// Verify two floating-point buffers are approximately equal
inline void requireBuffersEqual(const std::vector<float>& a,
                                const std::vector<float>& b,
                                float epsilon = 1e-5f) {
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::abs(a[i] - b[i]) < epsilon);
    }
}

// Create a simple test grid
inline nanovdb::GridHandle<nanovdb::HostBuffer> createTestGrid(uint32_t size) {
    nanovdb::GridBuilder<float> builder(0.0f);
    for (uint32_t x = 0; x < size; ++x) {
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t z = 0; z < size; ++z) {
                builder.setValue({x, y, z}, 1.0f);
            }
        }
    }
    return builder.getHandle();
}
```
