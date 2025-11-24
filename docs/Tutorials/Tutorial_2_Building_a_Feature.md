# Tutorial 2: Building a Feature

In this tutorial, we will implement a **Diffusion** process. Diffusion spreads values from high concentration to low concentration, blurring the field over time.

## The Physics

The diffusion equation is:
$$ \frac{\partial \phi}{\partial t} = D \nabla^2 \phi $$

Discretized using a 7-point stencil (central difference):
$$ \phi_{next} = \phi_{curr} + \Delta t \cdot D \cdot (\phi_{x+1} + \phi_{x-1} + \phi_{y+1} + \phi_{y-1} + \phi_{z+1} + \phi_{z-1} - 6\phi_{curr}) $$

## Step 1: Define the Stencil

We need to translate this math into GLSL using FluidLoom's DSL.

```lua
local diffusion_code = [[
    void main() {
        // Read center value
        float center = field_read("density", 0, 0, 0);
        
        // Read neighbors (offset x, y, z)
        float left   = field_read("density", -1,  0,  0);
        float right  = field_read("density",  1,  0,  0);
        float down   = field_read("density",  0, -1,  0);
        float up     = field_read("density",  0,  1,  0);
        float back   = field_read("density",  0,  0, -1);
        float front  = field_read("density",  0,  0,  1);
        
        // Laplacian
        float laplacian = left + right + down + up + back + front - 6.0 * center;
        
        // Update
        float dt = 0.1;
        float diffusion_rate = 0.5;
        float next = center + dt * diffusion_rate * laplacian;
        
        field_write("density", next);
    }
]]
```

## Step 2: Create the Script

Create `diffusion.lua`:

```lua
local engine = SimulationEngine.new()
engine:initialize()
engine:create_grid({64, 64, 64})
engine:register_field("density", "float")

-- Initialize a density source in the center
-- (In a real app, we'd use an initialization kernel, but let's assume
-- the engine provides a way to set initial values or we rely on default 0)

engine:register_stencil("diffusion", diffusion_code)

-- Run
engine:run(50)
```

## Step 3: Run and Verify

Run the script. While we can't visualize the output yet (see Tutorial 4), the simulation should run without errors.

## Key Takeaways

- **`field_read(name, dx, dy, dz)`**: Access neighbor voxels relative to the current thread.
- **`field_write(name, value)`**: Write the result for the current voxel.
- **Automatic Parallelization**: You write code for *one* voxel; the engine runs it for *all* active voxels in parallel.
