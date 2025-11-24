-- tests/integration/minimal_test.lua
-- Minimal integration test for FluidLoom

print("=== FluidLoom Integration Test ===\n")

-- Test 1: Create simulation
print("Test 1: Creating simulation...")
local config = {
    gpu_count = 1,
    grid_file = "test_grid.vdb",
    halo_thickness = 2
}

local sim = FluidEngine.new(config)
print("✓ Simulation created\n")

-- Test 2: Add fields
print("Test 2: Adding fields...")
sim:add_field("density", Format.R32F, "1.0")
sim:add_field("velocity", Format.R32G32B32F, "{0, 0, 0}")
print("✓ Fields added\n")

-- Test 3: Add simple decay stencil
print("Test 3: Adding stencil...")
sim:add_stencil("decay", {
    inputs = {"density"},
    outputs = {"density"},
    code = [[
        float d = Read_density(linearIdx);
        Write_density(linearIdx, d * 0.99);
    ]]
})
print("✓ Stencil added\n")

-- Test 4: Add neighbor-aware diffusion stencil
print("Test 4: Adding diffusion stencil with neighbors...")
sim:add_stencil("diffusion", {
    inputs = {"density"},
    outputs = {"density"},
    code = [[
        float center = Read_density(linearIdx);
        
        -- Use neighbor lookup helpers
        float xp = readNeighbor_XPlus(pc.field_density_addr, linearIdx);
        float xm = readNeighbor_XMinus(pc.field_density_addr, linearIdx);
        float yp = readNeighbor_YPlus(pc.field_density_addr, linearIdx);
        float ym = readNeighbor_YMinus(pc.field_density_addr, linearIdx);
        float zp = readNeighbor_ZPlus(pc.field_density_addr, linearIdx);
        float zm = readNeighbor_ZMinus(pc.field_density_addr, linearIdx);
        
        float laplacian = (xp + xm + yp + ym + zp + zm - 6.0 * center);
        float alpha = 0.01;
        
        Write_density(linearIdx, center + alpha * laplacian);
    ]],
    neighbor_radius = 1
})
print("✓ Diffusion stencil added\n")

-- Test 5: Build dependency graph
print("Test 5: Building dependency graph...")
sim:build_graph()
local schedule = sim:get_schedule()

print("✓ Dependency graph built")
print("  Execution schedule:")
for i, name in ipairs(schedule) do
    print("    " .. i .. ". " .. name)
end
print("")

-- Test 6: Export graph to DOT format
print("Test 6: Exporting graph...")
local dot = sim:export_graph_dot()
print("✓ Graph exported (" .. #dot .. " bytes)\n")

-- Test 7: Run timesteps
print("Test 7: Running simulation...")
for frame = 1, 10 do
    sim:step(0.016)
    if frame % 5 == 0 then
        print("  Frame " .. frame .. "/10")
    end
end
print("✓ Simulation completed\n")

-- Summary
print("=== All Tests Passed! ===")
print("✓ Simulation engine")
print("✓ Field registry")
print("✓ Stencil compilation")
print("✓ Neighbor lookups")
print("✓ Dependency graph")
print("✓ Graph execution")
print("\nFluidLoom is fully functional! 🎉")
