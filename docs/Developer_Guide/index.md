# Developer Guide

Welcome to the FluidLoom Developer Guide. This section provides all the information you need to set up your environment, understand the development workflow, and contribute to the codebase.

## Contents

- **[Setup & Installation](Setup_and_Installation.md)**: Get your development environment ready.
- **[Development Workflow](Development_Workflow.md)**: Learn about our Git workflow and code review process.
- **[Code Standards](Code_Standards.md)**: Style guides and best practices for C++ and Lua.
- **[Testing Guide](Testing_Guide.md)**: How to write and run tests.
- **[Building & Deployment](Building_and_Deployment.md)**: Build instructions and deployment strategies.
- **[Troubleshooting](Troubleshooting.md)**: Common issues and how to resolve them.

## Quick Start

If you already have the prerequisites installed:

```bash
git clone https://github.com/karthikt/fluidloom.git
cd fluidloom
mkdir build && cd build
cmake ..
make -j$(nproc)
./fluidloom_app ../tests/integration/minimal_test.lua
```
