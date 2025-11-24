# CMakeLists.txt for tools

# Test grid generator
add_executable(create_test_grid create_test_grid.cpp)

# Find OpenVDB and TBB properly
find_package(OpenVDB QUIET)
find_package(TBB QUIET)

if(OpenVDB_FOUND AND TBB_FOUND)
    target_link_libraries(create_test_grid PRIVATE OpenVDB::openvdb TBB::tbb)
else()
    # Fallback to system libraries if find_package fails
    target_include_directories(create_test_grid PRIVATE /opt/homebrew/include)
    target_link_directories(create_test_grid PRIVATE /opt/homebrew/lib)
    target_link_libraries(create_test_grid PRIVATE openvdb tbb)
endif()

install(TARGETS create_test_grid DESTINATION bin)
