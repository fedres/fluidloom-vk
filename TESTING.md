# Testing Guide - FluidLoom

## Quick Start

### Local Development (macOS)

Run non-GPU tests locally:
```bash
./run_tests.sh
```

Expected result: **15/28 tests pass**
- 11 DependencyGraph/DAG tests
- 3 Helper utility tests
- 1 Domain splitter initialization test

### Run Specific Test Categories

```bash
# DependencyGraph/DAG tests
./build/tests/fluidloom_tests "[graph]"

# Helper/utility tests
./build/tests/fluidloom_tests "[helpers]"

# Domain decomposition tests
./build/tests/fluidloom_tests "[domain]"

# Show all available tests
./build/tests/fluidloom_tests --list-tests
```

---

## Continuous Integration (GitHub Actions)

### Automated Testing
- **Trigger**: Push to `main` branch or open pull request
- **Platform**: Ubuntu 22.04 (Linux with Vulkan support)
- **Expected Result**: **28/28 tests pass** ✅

### View Results
1. Go to: https://github.com/fedres/fluidloom-vk/actions
2. Click on "FluidLoom Tests" workflow
3. Select the latest run to see detailed output
4. Download artifacts for test results

### Manual Trigger
1. Go to Actions tab
2. Select "FluidLoom Tests"
3. Click "Run workflow" button
4. Choose branch and click "Run"

---

## Test Coverage

### Tests Passing on Both Platforms (15/28)

**DependencyGraph/DAG Tests** (11)
- ✅ DAG creation with single node
- ✅ DAG with linear dependency chain
- ✅ DAG with parallel operations
- ✅ DAG with diamond dependency
- ✅ Cycle detection - simple cycle
- ✅ Cycle detection - complex cycle
- ✅ Cycle detection - no cycle
- ✅ Self-loop detection
- ✅ DAG export to DOT format
- ✅ Empty DAG handling
- ✅ Large DAG scheduling

**Helper/Utility Tests** (3)
- ✅ Buffer comparison
- ✅ Buffer size mismatch
- ✅ Tolerance comparison

**Domain Splitter Tests** (1)
- ✅ Domain splitter initialization

### Tests Failing on macOS (13/28)
*Due to MoltenVK/volk headless compatibility issue*

**Core Infrastructure** (6)
- ❌ Logger initialization
- ❌ Vulkan context creation
- ❌ Memory allocator initialization
- ❌ Buffer allocation and readback
- ❌ Grid creation
- ❌ Gradient grid creation

**Domain Decomposition** (4)
- ❌ Single GPU domain decomposition
- ❌ Domain neighbor computation
- ❌ Load balancing with gradient grid
- ❌ Empty domain handling

**NanoVDB/Field Registry** (3)
- ❌ Field registration
- ❌ Multiple field registration
- ❌ Field lookup by name

### Tests Passing on Linux (28/28)
All tests pass when run on Linux with Vulkan SDK installed.

---

## Troubleshooting

### Local Tests Failing: `volk` initialization error
This is expected on macOS due to MoltenVK headless limitations.
Use GitHub Actions for full test coverage.

### GitHub Actions Not Running
1. Check that `.github/workflows/tests.yml` exists in your repo
2. Verify branch is `main`, `master`, or `develop`
3. Go to repo Settings → Actions → ensure "Actions" is enabled
4. Click "Run workflow" manually to trigger

### How to Read Test Output

```bash
# Verbose output with all assertions
./build/tests/fluidloom_tests -s

# Show only failures
./build/tests/fluidloom_tests

# Run specific test
./build/tests/fluidloom_tests "Cycle detection - simple cycle"

# Show test names only
./build/tests/fluidloom_tests --list-tests
```

---

## Building Locally

```bash
# Configure
mkdir -p build
cd build
cmake .. -DCMAKE_CXX_STANDARD=20

# Build
cmake --build . -j$(nproc)

# Run tests
./tests/fluidloom_tests
```

---

## CI/CD Pipeline

### What Gets Tested
✅ Compilation without warnings (Release mode)
✅ All 28 unit tests
✅ Code formatting (clang-format)

### Artifacts Preserved
- Test results files (30 days retention)
- Available in GitHub Actions UI

### Build Optimization
- Uses Ninja build system for faster builds
- Release mode for optimal performance
- Parallel compilation with `-j$(nproc)`

---

## Key Files

| File | Purpose |
|------|---------|
| `.github/workflows/tests.yml` | GitHub Actions workflow config |
| `run_tests.sh` | Local test runner with environment setup |
| `Dockerfile.tests` | Docker image for containerized tests |
| `tests/main.cpp` | Test entry point with Vulkan setup |
| `tests/VulkanFixture.hpp` | Vulkan test infrastructure |

---

## Expected GitHub Actions Output

```
✅ Dependencies installed
✅ Vulkan SDK downloaded and configured
✅ CMake configure successful
✅ Build successful (0 warnings)
✅ 28 tests executed
✅ All 28 tests PASSED
✅ Code formatting validated
```

---

## Questions?

For issues or questions about testing, check:
- CI/CD logs: GitHub Actions tab
- Local logs: Terminal output from `./run_tests.sh`
- Build output: `build/` directory
