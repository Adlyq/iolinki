#!/usr/bin/env bash
#
# Remove generated CMake build trees from the repository root.
#
# Each `cmake -B build_<something>` leaves a tree of 10-50 MB behind. The test
# and quality scripts create several per run, and investigations add more, so
# the root directory fills up over time. This tool only touches directories that
# are recognisably build trees (a CMakeCache.txt / CMakeFiles at the top, or a
# directory holding per-configuration trees such as build_matrix/).
#
# Usage: tools/clean_builds.sh [--dry-run | --yes]
#   --dry-run  list what would be removed (default)
#   --yes      remove the listed trees

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

total_kb=0
found=0

while IFS= read -r dir; do
    [ -z "$dir" ] && continue
    if [ ! -f "$dir/CMakeCache.txt" ] && [ ! -d "$dir/CMakeFiles" ] &&
        ! compgen -G "$dir/*/CMakeCache.txt" > /dev/null; then
        continue
    fi
    found=$((found + 1))
    kb="$(du -sk -- "$dir" | cut -f1)"
    total_kb=$((total_kb + kb))
    printf '%8s  %s\n' "$(du -sh -- "$dir" | cut -f1)" "$dir"
    if [ "$mode" = "delete" ]; then
        rm -rf -- "$dir"
    fi
done < <(find . -mindepth 1 -maxdepth 1 -type d -name 'build*' | sort)

if [ "$found" -eq 0 ]; then
    echo "build trees: none in $ROOT"
    exit 0
fi

if [ "$mode" = "delete" ]; then
    echo "removed $found build tree(s) (~$((total_kb / 1024)) MB)"
else
    echo "would remove $found build tree(s) (~$((total_kb / 1024)) MB); rerun with --yes"
fi
