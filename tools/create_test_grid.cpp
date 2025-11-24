// tools/create_test_grid.cpp
// Creates a simple test grid with OpenVDB for FluidLoom testing

#include <openvdb/openvdb.h>
#include <openvdb/tools/LevelSetSphere.h>
#include <iostream>

int main() {
    // Initialize OpenVDB
    openvdb::initialize();
    
    std::cout << "Creating test grid for FluidLoom..." << std::endl;
    
    // Create a simple 64³ grid with a sphere in the center
    float radius = 12.0f;
    openvdb::Vec3f center(16.0f, 16.0f, 16.0f);
    
    // Create level set sphere
    auto grid = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
        radius, center, /*voxelSize=*/1.0f, /*halfWidth=*/3.0f);
    
    grid->setName("density");
    grid->setGridClass(openvdb::GRID_LEVEL_SET);
    
    // Save to file
    openvdb::io::File file("test_grid.vdb");
    openvdb::GridPtrVec grids;
    grids.push_back(grid);
    file.write(grids);
    file.close();
    
    std::cout << "✓ Created test_grid.vdb" << std::endl;
    std::cout << "  - Grid: " << grid->getName() << std::endl;
    std::cout << "  - Active voxels: " << grid->activeVoxelCount() << std::endl;
    std::cout << "  - Bounding box: " << grid->evalActiveVoxelBoundingBox() << std::endl;
    
    return 0;
}
