#!/usr/bin/env bash
#
# Remove generated CMake build trees.
#
# Every build goes to `build/<name>` (build/quality, build/matrix,
# build/coverage, build/wcet, build/size, ...), so the whole tree is one
# directory that git already ignores. This tool reports its size and removes it;
# it also sweeps `build_*` trees left at the repository root by older scripts.
#
# Usage: tools/clean_builds.sh [--dry-run | --yes]
#   --dry-run  list what would be removed (default)
#   --yes      remove it

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

mode="dry"
case "${1:-}" in
    "" | --dry-run) mode="dry" ;;
    --yes | -y) mode="delete" ;;
    *)
        echo "usage: $0 [--dry-run|--yes]" >&2
        exit 2
        ;;
esac

found=0

if [ -d build ]; then
    found=$((found + 1))
    printf '%8s  %s\n' "$(du -sh -- build | cut -f1)" "build/"
    [ "$mode" = "delete" ] && rm -rf -- build
fi

# Older scripts created trees next to the sources. Only remove directories that
# are recognisably CMake trees, so a source directory that happens to start with
# "build" is left alone.
while IFS= read -r dir; do
    [ -z "$dir" ] && continue
    if [ ! -f "$dir/CMakeCache.txt" ] && [ ! -d "$dir/CMakeFiles" ] &&
        ! compgen -G "$dir/*/CMakeCache.txt" > /dev/null; then
        continue
    fi
    found=$((found + 1))
    printf '%8s  %s\n' "$(du -sh -- "$dir" | cut -f1)" "$dir"
    [ "$mode" = "delete" ] && rm -rf -- "$dir"
done < <(find . -mindepth 1 -maxdepth 1 -type d -name 'build_*' | sort)

if [ "$found" -eq 0 ]; then
    echo "build trees: none in $ROOT"
    exit 0
fi

if [ "$mode" = "delete" ]; then
    echo "removed $found build tree(s)"
else
    echo "would remove $found build tree(s); rerun with --yes"
fi
