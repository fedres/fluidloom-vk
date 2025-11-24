# Infrastructure

## Deployment Topology

FluidLoom can be deployed in various configurations depending on the scale of the simulation.

### Single Node (Workstation)
- **Hardware**: 1 CPU, 1 or more GPUs.
- **Communication**: Shared memory / P2P (if multi-GPU).
- **Use Case**: Development, small to medium simulations.

### Multi-Node (HPC Cluster)
- **Hardware**: Multiple nodes, each with 1+ GPUs.
- **Communication**: MPI (Message Passing Interface) over InfiniBand or Ethernet.
- **Use Case**: Large-scale simulations (billions of voxels).

## Hardware Requirements

### Minimum
- **CPU**: x86_64 or ARM64 (4 cores).
- **RAM**: 8 GB.
- **GPU**: Vulkan 1.2 capable, 4 GB VRAM.
- **Storage**: 10 GB (for output data).

### Recommended
- **CPU**: 16+ cores.
- **RAM**: 32 GB+.
- **GPU**: Vulkan 1.3 capable, 16 GB+ VRAM (NVIDIA RTX 3080/4090 or equivalent).
- **Storage**: NVMe SSD.

## Cloud Deployment

FluidLoom can run on GPU instances in the cloud (AWS EC2 g4/p3/p4, Google Cloud A2/T4).

- **Image**: Ubuntu 22.04 with NVIDIA Drivers and Docker.
- **Container**: Use the provided Dockerfile.
