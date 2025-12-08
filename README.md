# ROCm/HIP Port Walkthrough

This document outlines the changes made to port CuCLARK to ROCm (AMD GPUs) and provides instructions for compilation and verification.

## Changes Made

### Source Code (`src/CuClarkDB.cu`, `src/CuClarkDB.cuh`)
-   Replaced all CUDA API calls with HIP equivalents (e.g., `cudaMalloc` -> `hipMalloc`).
-   Replaced `cuda_runtime.h` with `hip/hip_runtime.h`.
-   Converted `__ballot_sync` and `__shfl_sync` masks to `~0ULL` to support 64-bit warps (Wave64) on AMD GPUs.
-   Replaced `nvcc`-specific logic with standard HIP logic where applicable.

### Build System (`src/Makefile`)
-   Replaced `nvcc` with `hipcc`.
-   Updated flags to remove NVIDIA-specific architecture generation (`-gencode`).
-   Use `hipcc` for linking.

## Verification Instructions

### 1. Compile the Code

Ensure you have ROCm installed and `hipcc` is in your PATH.

```bash
make clean
make
```

### 2. Run Tests

After compilation, verify basic functionality:

```bash
./exe/cuCLARK --help
```

To run a classification (assuming you have a database built):

```bash
./exe/cuCLARK -k 31 -T <target_file> -D <database_files> -P <reads_file> -R <results_prefix>
```

### Notes
-   The port assumes `hipcc` will handle architecture selection (defaulting to the installed GPU via `amdgpu-arch` or similar mechanisms). If you need to target a specific architecture (e.g., `gfx90a`), uncomment and edit the line in `src/Makefile`:
    ```makefile
    # HIPCCFLAGS += --offload-arch=gfx90a
    ```
-   Warp size logic in kernels was inspected. While AMD uses 64-thread wavefronts, the code should adapt nicely due to explicit warp size constants being replaced or handled by intrinsic semantics. The masks for ballot/shuffle were updated to 64-bit.