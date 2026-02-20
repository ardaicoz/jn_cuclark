#!/bin/bash
# install.sh - Bootstrap installation script for CuCLARK
# Can be run without any existing binaries

set -e  # Exit on error

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  CuCLARK Installation"
echo "========================================"
echo ""

# 1. Environment checks
echo "1. Checking build environment..."

# Check for required tools
if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ not found. Please install g++ (apt-get install g++)"
    exit 1
fi

if ! command -v make &> /dev/null; then
    echo "ERROR: make not found. Please install make (apt-get install build-essential)"
    exit 1
fi

echo "   ✓ g++ found: $(g++ --version | head -1)"
echo "   ✓ make found: $(make --version | head -1)"

# Check for CUDA (warning only, not fatal)
if ! command -v nvcc &> /dev/null; then
    echo "   ⚠ nvcc not found. CUDA components will not build."
    echo "     You can still build 'arda' orchestrator with: make -C app arda"
    CUDA_AVAILABLE=0
else
    CUDA_VERSION=$(nvcc --version | grep "release" | sed 's/.*release //' | sed 's/,.*//')
    echo "   ✓ CUDA detected: $CUDA_VERSION"
    CUDA_AVAILABLE=1
fi

# Check for MPI (optional)
if command -v mpicxx &> /dev/null; then
    echo "   ✓ MPI detected: $(mpicxx --version | head -1)"
    MPI_AVAILABLE=1
else
    echo "   ⚠ MPI not found (optional). Cluster mode will not be available."
    MPI_AVAILABLE=0
fi

echo ""

# 2. Create required directories
echo "2. Creating directory structure..."
mkdir -p bin logs results config data
echo "   Created: bin/ logs/ results/ config/ data/"
echo ""

# 3. Build components
echo "3. Building CuCLARK components..."

if [ "$CUDA_AVAILABLE" -eq 1 ]; then
    echo "   Building: cuCLARK + arda..."
    if make -C app all; then
        echo "   ✓ cuCLARK core and arda built successfully"
    else
        echo "   ✗ Build failed"
        exit 1
    fi
else
    echo "   Building: arda only (no CUDA)..."
    if make -C app arda; then
        echo "   ✓ arda built successfully"
    else
        echo "   ✗ Build failed"
        exit 1
    fi
fi

if [ "$MPI_AVAILABLE" -eq 1 ]; then
    echo "   Building: arda-mpi..."
    if make -C app arda-mpi; then
        echo "   ✓ arda-mpi built successfully"
    else
        echo "   ⚠ arda-mpi build failed (not critical)"
    fi
fi

echo ""

# 4. Verify binaries
echo "4. Verifying installation..."
REQUIRED_BINS="bin/arda"
if [ "$CUDA_AVAILABLE" -eq 1 ]; then
    REQUIRED_BINS="$REQUIRED_BINS bin/cuCLARK bin/cuCLARK-l bin/getTargetsDef bin/getAccssnTaxID bin/getfilesToTaxNodes bin/getAbundance"
fi

ALL_FOUND=1
for bin in $REQUIRED_BINS; do
    if [ -x "$bin" ]; then
        echo "   ✓ $bin"
    else
        echo "   ✗ $bin (missing or not executable)"
        ALL_FOUND=0
    fi
done

echo ""

# 5. Write installation marker
if [ "$ALL_FOUND" -eq 1 ]; then
    echo "INSTALLED=1" > logs/ardacpp_log.txt
    echo "========================================"
    echo "Installation completed successfully!"
    echo "========================================"
    echo ""
    echo "Next steps:"
    echo "  1. Verify: ./bin/arda --verify"
    echo "  2. Setup database: ./bin/arda -d <database_path>"
    echo "  3. See usage: ./bin/arda -h"
    echo ""
else
    echo "INSTALLED=0" > logs/ardacpp_log.txt
    echo "========================================"
    echo "Installation completed with warnings"
    echo "========================================"
    echo "Some binaries are missing."
    exit 1
fi
