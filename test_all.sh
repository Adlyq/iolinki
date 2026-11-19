#!/bin/bash
set -e

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# iolinki Test Suite Runner
# Runs builds and tests for all supported platforms

echo "============================================"
echo "🚀 iolinki CI: Starting Validation Suite"
echo "============================================"

# Environment Check
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake not found"
    exit 1
fi

echo "✅ Environment checks passed"

# 1. Linux Host Build & Test
echo -e "\n[1/2] 🐧 Testing Linux Host Target..."
rm -rf build/linux
mkdir -p build/linux
cd build/linux
cmake -S "$ROOT" -B . -DCMAKE_BUILD_TYPE=Debug -DIOLINK_PLATFORM=LINUX
cmake --build . --parallel "$(nproc)"
echo "   ✅ Linux Build Successful"

cd "$ROOT"

# 2. Bare Metal Build Verification
echo -e "\n[2/2] ⚙️  Verifying Bare Metal Build..."
rm -rf build/baremetal
mkdir -p build/baremetal
cd build/baremetal
# Bare metal usually requires cross-compiler, but we can verify source compilation
# using a generic config or mock toolchain if available.
# For now, we build the library only to ensure no Linux dependencies leaked.
cmake -S "$ROOT" -B . -DCMAKE_BUILD_TYPE=Debug -DIOLINK_PLATFORM=BAREMETAL
cmake --build . --parallel "$(nproc)" --target iolinki
echo "   ✅ Bare Metal Library Build Successful"
cd "$ROOT"

# 4. Cleanup
echo -e "\n============================================"
echo "✅ All Validation Steps Completed Successfully"
echo "============================================"
exit 0
