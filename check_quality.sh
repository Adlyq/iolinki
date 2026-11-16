#!/bin/bash
set -e

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

echo "============================================"
echo "🔍 iolinki Code Quality & Safety Check"
echo "============================================"

# 1. Compiler Warnings (Strict)
# We rely on CMake to set -Wall -Wextra -Werror via the build system
echo -e "\n[1/10] 🛡️  Verifying Compilation Warnings..."
BUILD_DIR="${IOLINKI_BUILD_DIR:-$ROOT/build/quality}"
case "${BUILD_DIR}" in
    /*) ;;
    *) BUILD_DIR="$ROOT/${BUILD_DIR}" ;;
esac
RELEASE_DIR="${BUILD_DIR}_release"
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cached_src=$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "${BUILD_DIR}/CMakeCache.txt" | cut -d= -f2-)
    if [ -n "${cached_src}" ] && [ "${cached_src}" != "${ROOT}" ]; then
        rm -rf "${BUILD_DIR}"
    fi
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
# Enable Strict Mode (we need to add this flag support to CMakeLists or force it here)
cmake -S "${ROOT}" -B . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow"
# cmake --build is generator-agnostic (the host may default to Ninja, where a
# bare `make` finds no Makefile).
if cmake --build . --parallel "$(nproc)"; then
    echo "   ✅ Strict Compilation Passed"
else
    echo "   ❌ Strict Compilation FAILED"
    exit 1
fi
cd "${ROOT}"

# Debug (-O0) does not emit some -O2/-O3-only diagnostics
# (e.g. -Wmaybe-uninitialized), so repeat the strict build at -O3.
rm -rf "${RELEASE_DIR}"
cmake -S . -B "${RELEASE_DIR}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow"
if cmake --build "${RELEASE_DIR}" --parallel "$(nproc)"; then
    echo "   ✅ Release (-O3) Compilation Passed"
else
    echo "   ❌ Release (-O3) Compilation FAILED"
    exit 1
fi

# 2. Static Analysis (Cppcheck)
echo -e "\n[2/10] 🧹 Running Static Analysis (Cppcheck)..."
if command -v cppcheck &> /dev/null; then
    # Run cppcheck with rigorous settings
    # --enable=all: Enable all checks (style, performance, portability, etc.)
    # --suppress=missingIncludeSystem: Don't fail on missing standard headers
    # --error-exitcode=1: Fail script if errors found
    # --addon=misra: Ideally we would use this, but it requires a rules text file.
    #                We'll use --enable=warning,style,performance,portability for now.

    # Suppress findings that depend on the compile-time feature set (cppcheck
    # analyses one configuration at a time).
    if cppcheck --enable=warning,style,performance,portability \
             --error-exitcode=1 \
             --suppress=missingIncludeSystem \
             --suppress=constParameterPointer \
             --suppress=knownConditionTrueFalse \
             --inline-suppr \
             --quiet \
             -I include \
             src/ examples/; then
        echo "   ✅ Static Analysis Passed"
    else
        echo "   ❌ Static Analysis FAILED"
        exit 1
    fi
else
    echo "   ⚠️ Cppcheck not installed. Skipping static analysis."
    echo "      Install with: sudo apt-get install cppcheck"
fi

# 3. MISRA C:2012 Check (Cppcheck addon)
echo -e "\n[3/10] 📏 Running MISRA C:2012 Check..."
if command -v cppcheck &> /dev/null; then
    # Use cppcheck MISRA addon if available in the environment.
    # If the addon is not present, fail in enforce mode.
    MISRA_ADDON=""
    for path in /usr/share/cppcheck/addons/misra.py /usr/lib/cppcheck/addons/misra.py; do
        if [ -f "$path" ]; then
            MISRA_ADDON="$path"
            break
        fi
    done

    if [ -n "$MISRA_ADDON" ]; then
        # The MISRA addon is very noisy on this C99 codebase (~1500 pre-existing
        # findings). Report-only by default; set IOLINKI_MISRA_ENFORCE=1 to make
        # violations fatal.
        if [ "${IOLINKI_MISRA_ENFORCE}" = "1" ]; then
            cppcheck --addon=misra \
                     --enable=warning,style,performance,portability \
                     --error-exitcode=1 \
                     --suppress=missingIncludeSystem \
                     --suppress=constParameterPointer \
                     --suppress=knownConditionTrueFalse \
                     --inline-suppr \
                     --quiet \
                             -I include \
                     src/ examples/
            echo "   ✅ MISRA Check Passed (enforced)"
        else
            cppcheck --addon=misra \
                     --enable=warning,style,performance,portability \
                     --suppress=missingIncludeSystem \
                     --suppress=constParameterPointer \
                     --suppress=knownConditionTrueFalse \
                     --inline-suppr \
                     --quiet \
                             -I include \
                     src/ examples/ || true
            echo "   ⚠️ MISRA report generated (non-enforcing; set IOLINKI_MISRA_ENFORCE=1 to fail)"
        fi
    else
        if [ "${IOLINKI_MISRA_ENFORCE}" = "1" ]; then
            echo "   ❌ MISRA addon not available in cppcheck. Enforced mode fails."
            exit 1
        else
            echo "   ⚠️ Cppcheck MISRA addon not available. Skipping MISRA checks."
        fi
    fi
else
    echo "   ⚠️ Cppcheck not installed. Skipping MISRA checks."
fi

# 4. Code Formatting Check
echo -e "\n[4/10] 🎨 Checking Code Formatting..."
if command -v clang-format &> /dev/null; then
    # clang-format is pinned to 23.1.1 in .pre-commit-config.yaml and the Docker
    # images. Other major versions reformat differently, so do not silently skip:
    # warn and still run the check.
    CF_VERSION=$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    if [ "${CF_VERSION}" != "23.1.1" ]; then
        echo "   ⚠️ clang-format ${CF_VERSION} != pinned 23.1.1 (results may differ)"
        echo "      Install it with: pip install clang-format==23.1.1"
    fi
    if find src include tests examples -type f \( -name "*.c" -o -name "*.h" \) -print0 \
        | xargs -0 clang-format --dry-run --Werror; then
        echo "   ✅ Code Formatting Passed"
    else
        echo "   ❌ Code Formatting FAILED"
        exit 1
    fi
else
    echo "   ⚠️ clang-format not installed. Skipping check."
    echo "      Install the pinned version: pip install clang-format==23.1.1"
fi

# 5. File Size Gate
echo -e "\n[5/10] 📐 Checking File Size (code lines per file)..."
if python3 tools/check_file_size.py --selftest && python3 tools/check_file_size.py; then
    echo "   ✅ File Size Passed"
else
    echo "   ❌ File Size FAILED"
    exit 1
fi

# 6. Doxygen Warning Check
echo -e "\n[6/10] 📚 Checking Doxygen Warnings..."
if command -v doxygen &> /dev/null; then
    doxygen Doxyfile > /dev/null 2> doxygen.log
    if grep -q "warning:" doxygen.log; then
        echo "   ❌ Doxygen warnings found:"
        grep "warning:" doxygen.log
        exit 1
    else
        echo "   ✅ Doxygen Check Passed"
        rm doxygen.log
    fi
else
     echo "   ⚠️ Doxygen not installed. Skipping check."
fi

echo -e "\n[7/10] 🐚 Linting Shell Scripts (Shellcheck)..."
if command -v shellcheck &> /dev/null; then
    # Maintained CI/test scripts (add new ones here). tools/add_copyright_headers.sh
    # is a legacy one-shot helper with a known SC2218 false positive.
    lint_scripts=(tools/build_matrix.sh tools/coverage.sh tools/diff_matrix.sh
                  tools/check_ownership.sh tools/check_test_whitebox.sh
                  tools/check_test_gates.sh tools/clean_builds.sh
                  tools/test_install_consumer.sh
                  tools/wcet_run.sh tools/check_profile_abi.sh
                  tools/matrix_configs.sh
                  test_all.sh check_quality.sh .docker_test_entrypoint.sh)
    if shellcheck -S warning "${lint_scripts[@]}"; then
        echo "   ✅ Shellcheck Passed"
    else
        echo "   ❌ Shellcheck FAILED"
        exit 1
    fi
else
    echo "   ⚠️ Shellcheck not installed. Skipping check."
    echo "      Arch: sudo pacman -S shellcheck-static / Debian: apt-get install shellcheck"
fi

echo -e "\n[8/10] 🔒 DLL Ownership Isolation..."
if ./tools/check_ownership.sh; then
    echo "   ✅ Ownership Isolation Passed"
else
    echo "   ❌ Ownership Isolation FAILED"
    exit 1
fi

echo -e "\n[9/10] 🧱 Test Gates (white-box, teardown)..."
if ./tools/check_test_whitebox.sh --selftest && ./tools/check_test_whitebox.sh &&
    ./tools/check_test_gates.sh; then
    echo "   ✅ Test Gates Passed"
else
    echo "   ❌ Test Gates FAILED"
    exit 1
fi

echo -e "\n[10/10] 📦 Install and Consume..."
if ./tools/test_install_consumer.sh; then
    echo "   ✅ Install/Consume Passed"
else
    echo "   ❌ Install/Consume FAILED"
    exit 1
fi

echo -e "\n============================================"
echo "✅ Code Quality Checks Completed"
echo "============================================"
exit 0
