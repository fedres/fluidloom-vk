# Test configuration

# Find Catch2 testing framework
find_package(Catch2 CONFIG QUIET)
if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found - tests will be skipped")
    return()
endif()

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
        ${CMAKE_SOURCE_DIR}/external/openvdb/nanovdb
)

# Register tests with CTest
include(CTest)
# Note: catch_discover_tests requires newer Catch2 version, manually add tests for now
# catch_discover_tests(fluidloom_tests)

# Add test target to run with: cmake --build build --target test
enable_testing()
add_test(NAME FluidLoomTests COMMAND fluidloom_tests)

# Standalone integration tests
add_executable(shader_compilation_test integration/ShaderCompilationTest.cpp)
target_link_libraries(shader_compilation_test PRIVATE fluidloom)
target_include_directories(shader_compilation_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/openvdb/nanovdb
)

# Set environment variables for macOS tests
if(APPLE)
    set_target_properties(shader_compilation_test PROPERTIES
        ENVIRONMENT "VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
    )
    set_target_properties(fluidloom_tests PROPERTIES
        ENVIRONMENT "VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
    )
endif()

add_test(NAME ShaderCompilationTest COMMAND shader_compilation_test)
