#!/bin/bash
# Test runner script with proper Vulkan environment setup

# Set Vulkan ICD path for MoltenVK
export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json

# Optional: Set to verbose for debugging
# export VK_LOADER_DEBUG=all

echo "Running FluidLoom Tests"
echo "======================"
echo "VK_ICD_FILENAMES: $VK_ICD_FILENAMES"
echo ""

# Run the test executable with any passed arguments
./build/tests/fluidloom_tests "$@"
