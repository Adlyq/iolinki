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
