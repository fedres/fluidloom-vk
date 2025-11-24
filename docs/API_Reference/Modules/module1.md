# SimulationEngine API

The `SimulationEngine` class is the main entry point for the FluidLoom scripting API.

## Constructor

### `SimulationEngine.new()`
Creates a new instance of the engine.

**Returns**: `SimulationEngine` instance.

## Methods

### `initialize()`
Initializes the Vulkan context and internal systems. Must be called before any other method.

### `create_grid(dimensions)`
Creates a NanoVDB grid with the specified dimensions.

- **dimensions**: Table `{x, y, z}` specifying the domain size in voxels.

### `register_field(name, type)`
Allocates a new field buffer.

- **name**: String identifier for the field.
- **type**: String type (`"float"`, `"vec3"`, `"int"`).

### `register_stencil(name, code)`
Compiles a GLSL compute shader for a stencil operation.

- **name**: String identifier for the stencil.
- **code**: String containing the GLSL source code.

### `run(steps)`
Executes the simulation for a specified number of time steps.

- **steps**: Integer number of steps to run.

### `write_vdb(field_name, filepath)`
Exports a field to an OpenVDB file.

- **field_name**: Name of the field to export.
- **filepath**: Destination path.
