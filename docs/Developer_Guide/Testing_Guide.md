# Testing Guide

FluidLoom uses a combination of unit tests (C++) and integration tests (Lua scripts) to ensure correctness.

## Unit Tests

Unit tests are written using **Catch2**. They are located in `tests/unit/`.

### Running Unit Tests

1. Enable tests in CMake:
   ```bash
   cmake .. -DENABLE_TESTS=ON
   ```
2. Build:
   ```bash
   make
   ```
3. Run:
   ```bash
   ./tests/unit_tests
   ```

### Writing Unit Tests

Create a new `.cpp` file in `tests/unit/` and register it in `tests/CMakeLists.txt`.

```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/MathUtils.hpp"

TEST_CASE("MathUtils::clamp", "[math]") {
    REQUIRE(MathUtils::clamp(5, 0, 10) == 5);
    REQUIRE(MathUtils::clamp(-1, 0, 10) == 0);
    REQUIRE(MathUtils::clamp(11, 0, 10) == 10);
}
```

## Integration Tests

Integration tests are Lua scripts that run the full simulation engine. They are located in `tests/integration/`.

### Running Integration Tests

Pass the script path to the main executable:

```bash
./fluidloom_app ../tests/integration/minimal_test.lua
```

### Writing Integration Tests

Create a new `.lua` file in `tests/integration/`.

```lua
-- test_feature.lua
local engine = SimulationEngine.new()
engine:initialize()

-- Setup grid and fields
local grid = engine:create_grid({128, 128, 128})
engine:register_field("density", "float")

-- Define update rule
engine:register_stencil("advect", [[
    // GLSL code
    void main() { ... }
]])

-- Run simulation
engine:run(10) -- 10 steps

-- Verify results (if assertions are available in Lua binding)
```

## Test Data

Test data (e.g., initial VDB files) should be placed in `tests/data/`. Do not commit large binary files; use a script to generate them or download them.
