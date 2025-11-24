# Tutorial 1: Getting Started

In this tutorial, you will learn how to build FluidLoom and run a minimal simulation.

## Prerequisites
- Completed the [Setup Guide](../Developer_Guide/Setup_and_Installation.md).

## Step 1: Build the Engine

First, ensure you have a working build of the engine.

```bash
cd fluidloom
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Verify that `fluidloom_app` exists in the `build` directory.

## Step 2: Create a Simulation Script

Create a new file named `hello_fluid.lua` in the `scripts/` directory (or anywhere you like).

```lua
-- hello_fluid.lua

-- 1. Create the engine instance
local engine = SimulationEngine.new()

-- 2. Initialize (creates Vulkan context)
engine:initialize()

-- 3. Create a grid
-- Domain size: 128x128x128 voxels
engine:create_grid({128, 128, 128})

-- 4. Register a field
-- We'll track "density"
engine:register_field("density", "float")

-- 5. Define a simple stencil
-- This stencil just decays the density by 1% each step
local decay_code = [[
    void main() {
        float current = field_read("density");
        float next = current * 0.99;
        field_write("density", next);
    }
]]
engine:register_stencil("decay", decay_code)

-- 6. Run the simulation
print("Starting simulation...")
engine:run(100) -- Run for 100 steps
print("Simulation complete!")
```

## Step 3: Run the Simulation

Execute the script using the `fluidloom_app` executable.

```bash
./fluidloom_app hello_fluid.lua
```

## Output

You should see logs indicating the initialization of Vulkan, creation of the grid, compilation of the shader, and execution of the steps.

```
[info] Initializing SimulationEngine...
[info] Vulkan initialized successfully.
[info] Grid created: 128x128x128
[info] Field 'density' registered.
[info] Stencil 'decay' compiled.
Starting simulation...
[info] Step 1 complete.
...
[info] Step 100 complete.
Simulation complete!
```

## Next Steps

Now that you have the engine running, try [Tutorial 2: Building a Feature](Tutorial_2_Building_a_Feature.md) to implement a more interesting physical process.
