# Test configuration

# Find Catch2 testing framework
find_package(Catch2 REQUIRED)

# Test executable
add_executable(fluidloom_tests
    main.cpp
    TestCore.cpp
    TestDomain.cpp
    TestGraph.cpp
)

# Link against main library and test framework
target_link_libraries(fluidloom_tests
    PRIVATE
        fluidloom
        Catch2::Catch2WithMain
)

# Include test headers
target_include_directories(fluidloom_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

# Register tests with CTest
catch_discover_tests(fluidloom_tests)

# Add test target to run with: cmake --build build --target test
enable_testing()
add_test(NAME FluidLoomTests COMMAND fluidloom_tests)
