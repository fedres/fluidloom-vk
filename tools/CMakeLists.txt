# CMakeLists.txt for tools

# Test grid generator
add_executable(create_test_grid create_test_grid.cpp)
target_include_directories(create_test_grid PRIVATE /opt/homebrew/include)
target_link_directories(create_test_grid PRIVATE /opt/homebrew/lib)
target_link_libraries(create_test_grid PRIVATE openvdb tbb)

install(TARGETS create_test_grid DESTINATION bin)
