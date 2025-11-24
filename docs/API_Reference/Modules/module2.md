# Field API

The `Field` module provides functions for interacting with simulation fields.

## Functions

### `field_read(name, dx, dy, dz)`
Reads a scalar value from a field at a relative offset.

- **name**: String name of the field.
- **dx, dy, dz**: Integer offsets from the current voxel.
- **Returns**: Float value.

### `field_write(name, value)`
Writes a scalar value to a field at the current voxel position.

- **name**: String name of the field.
- **value**: Float value to write.

### `field_read_vec3(name, dx, dy, dz)`
Reads a vector value from a field.

- **name**: String name of the field.
- **dx, dy, dz**: Integer offsets.
- **Returns**: `vec3` (GLSL type).

### `field_write_vec3(name, value)`
Writes a vector value to a field.

- **name**: String name of the field.
- **value**: `vec3` value.

### `field_sample(name, x, y, z)`
Samples a field at an arbitrary floating-point coordinate using trilinear interpolation.

- **name**: String name of the field.
- **x, y, z**: World space coordinates.
- **Returns**: Float value.
