#!/bin/bash
set -e

root=/workspace
cd "$root"

rm -rf "$root/build/docker"

cmake -S "$root" -B "$root/build/docker" -DIOLINK_PLATFORM=LINUX
cmake --build "$root/build/docker"

# Run Unit Tests
(cd "$root/build/docker" && ctest --output-on-failure)

# Run Bare Metal Build Verification. Keep the source directory explicit: the
# old `cd`/`cd ..` left the working directory inside build/, so CMake looked for
# CMakeLists.txt there.
echo -e "\nRunning Bare Metal Build Verification..."
rm -rf "$root/build/bare"
cmake -S "$root" -B "$root/build/bare" -DIOLINK_PLATFORM=BAREMETAL
cmake --build "$root/build/bare"
export IOLINK_DEVICE_PATH=/workspace/build/docker/examples/host_demo/host_demo
# The virtual-master integration tests drive the device over a PTY and are
# timing-sensitive; bound each so a stuck PTY fails fast instead of hanging the
# whole CI run (observed stalling 40+ min on loaded runners). Normal runtime is
# well under 30s each.
run_vm_test() { timeout --kill-after=10 180 python3 "tools/virtual_master/$1"; }
run_vm_test test_automated_mandatory.py
run_vm_test test_pd_variable.py
run_vm_test test_conformance_error_injection.py
run_vm_test test_conformance_isdu.py
run_vm_test test_conformance_state_machine.py
run_vm_test test_conformance_timing.py
