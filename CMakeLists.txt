cmake_minimum_required(VERSION 3.15)
project(FluidLoom)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add external dependencies
add_subdirectory(external)

# Add source directories
add_subdirectory(src)
