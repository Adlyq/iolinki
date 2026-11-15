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

# Run Integration Tests
if [ -d "$ROOT/tools/virtual_master" ]; then
    export IOLINK_DEVICE_PATH="./examples/host_demo/host_demo"

    # 1. Type 1 Test
    if [ -f "$ROOT/tools/virtual_master/test_type1.py" ]; then
        echo "   🏃 Running Type 1 Integration Test..."
        if ! python3 "$ROOT/tools/virtual_master/test_type1.py"; then
            echo "   ❌ Type 1 Integration Test FAILED"
            exit 1
        fi
        echo "   ✅ Type 1 Integration Test Passed"
    fi

    # 2. Mandatory Indices Test
    if [ -f "$ROOT/tools/virtual_master/test_automated_mandatory.py" ]; then
        echo "   🏃 Running Mandatory Indices Integration Test..."
        if ! python3 "$ROOT/tools/virtual_master/test_automated_mandatory.py"; then
            echo "   ❌ Mandatory Indices Test FAILED"
            exit 1
        fi
        echo "   ✅ Mandatory Indices Test Passed"
    fi

    # 3. Variable PD & Persistence Test
    if [ -f "$ROOT/tools/virtual_master/test_pd_variable.py" ]; then
        echo "   🏃 Running Variable PD & Persistence Integration Test..."
        if ! python3 "$ROOT/tools/virtual_master/test_pd_variable.py"; then
            echo "   ❌ Variable PD Test FAILED"
            exit 1
        fi
        echo "   ✅ Variable PD Test Passed"
    fi

    # 4. IO-Link V1.1.5 Conformance Test Suite
    echo "   🏃 Running IO-Link V1.1.5 Conformance Tests..."

    if [ -f "$ROOT/tools/virtual_master/test_conformance_state_machine.py" ]; then
        echo "      → State Machine Conformance..."
        if ! python3 "$ROOT/tools/virtual_master/test_conformance_state_machine.py"; then
            echo "   ❌ State Machine Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f "$ROOT/tools/virtual_master/test_conformance_timing.py" ]; then
        echo "      → Timing Requirements..."
        if ! python3 "$ROOT/tools/virtual_master/test_conformance_timing.py"; then
            echo "   ❌ Timing Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f "$ROOT/tools/virtual_master/test_conformance_isdu.py" ]; then
        echo "      → ISDU Protocol Validation..."
        if ! python3 "$ROOT/tools/virtual_master/test_conformance_isdu.py"; then
            echo "   ❌ ISDU Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f "$ROOT/tools/virtual_master/test_conformance_error_injection.py" ]; then
        echo "      → Error Injection & Recovery..."
        if ! python3 "$ROOT/tools/virtual_master/test_conformance_error_injection.py"; then
            echo "   ❌ Error Injection Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f "$ROOT/tools/virtual_master/test_conformance_performance.py" ]; then
        echo "      → Performance & Stress Testing..."
        if ! python3 "$ROOT/tools/virtual_master/test_conformance_performance.py"; then
            echo "   ❌ Performance Conformance FAILED"
            exit 1
        fi
    fi

    echo "   ✅ All Conformance Tests Passed"
else
    echo "   ⚠️ Skipping Integration Tests (Tools directory not found)"
fi
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
