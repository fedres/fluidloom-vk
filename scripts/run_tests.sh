#!/bin/bash

# Test runner script for FluidLoom integration tests
# This script sets up the environment and runs tests with proper error handling

set -e  # Exit on any error

echo "=== FluidLoom Test Runner ==="
echo "Setting up environment for Vulkan tests..."

# Set up environment for macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # Ensure MoltenVK ICD is found
    export VK_ICD_FILENAMES="/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
    export VK_DRIVER_FILES="/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
    
    # Check if MoltenVK is installed
    if [ ! -f "$VK_ICD_FILENAMES" ]; then
        echo "ERROR: MoltenVK ICD not found at $VK_ICD_FILENAMES"
        echo "Please install MoltenVK via: brew install molten-vk"
        exit 1
    fi
    
    echo "Using MoltenVK ICD: $VK_ICD_FILENAMES"
fi

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "ERROR: Please run this script from the project root directory"
    exit 1
fi

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

cd build

# Always reconfigure to ensure tests are included
echo "Configuring CMake..."
cmake .. -G Ninja

# Build the tests
echo "Building tests..."
ninja

# Run the standalone integration test first
echo ""
echo "=== Running Shader Compilation Test ==="
if ./shader_compilation_test; then
    echo "✓ Shader compilation test PASSED"
else
    echo "✗ Shader compilation test FAILED"
    exit 1
fi

# Run the full test suite
echo ""
echo "=== Running Full Test Suite ==="
if ./fluidloom_tests; then
    echo "✓ All tests PASSED"
else
    echo "✗ Some tests FAILED"
    exit 1
fi

echo ""
echo "=== All tests completed successfully! ==="