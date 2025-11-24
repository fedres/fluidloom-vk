# Tutorial 3: Advanced Usage

In this tutorial, we will combine **Advection** and **Diffusion** to simulate smoke moving through air.

## Concepts

- **Advection**: Transporting a quantity (density) by a velocity field.
- **Velocity Field**: A vector field representing flow direction.

## Step 1: Register Fields

We need two fields: `density` (scalar) and `velocity` (vector).

```lua
engine:register_field("density", "float")
engine:register_field("velocity", "vec3")
```

## Step 2: Define Advection Stencil

Semi-Lagrangian advection involves tracing back along the velocity vector to find the value at the previous time step.

```lua
local advect_code = [[
    void main() {
        // Read velocity at current position
        vec3 vel = field_read_vec3("velocity", 0, 0, 0);
        
        // Backtrace: pos_prev = pos_curr - vel * dt
        // Note: This requires a sampler to interpolate values.
        // For this tutorial, we'll use a simplified integer offset approximation.
        
        int dx = int(-vel.x);
        int dy = int(-vel.y);
        int dz = int(-vel.z);
        
        float old_density = field_read("density", dx, dy, dz);
        
        field_write("density", old_density);
    }
]]
```

> **NOTE**: Real semi-Lagrangian advection requires trilinear interpolation, which is supported via `field_sample()`. We used integer lookups here for simplicity.

## Step 3: Chain Operations

We want to advect first, then diffuse.

```lua
engine:register_stencil("advect", advect_code)
engine:register_stencil("diffuse", diffusion_code) -- from Tutorial 2

-- Define the execution graph
engine:add_pass("advect")
engine:add_pass("diffuse")

engine:run(100)
```

## Step 4: Time Splitting

The engine executes passes sequentially for each time step.
1. `advect` reads `density(t)` and writes `density(t*)`.
2. `diffuse` reads `density(t*)` and writes `density(t+1)`.

This is handled automatically by the double-buffering system if configured correctly (ensure `diffuse` reads the output of `advect`).
