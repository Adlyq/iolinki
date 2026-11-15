#!/usr/bin/env bash
#
# A cmocka group teardown cannot fail a test binary.
#
# cmocka 2.x prints "[ FAILED ] GROUP TEARDOWN" for an assertion inside a group
# teardown but still exits 0, so `ctest` reports Passed. Every gate that runs at
# group level must therefore end the process itself. This check keeps that rule
# from silently regressing: a teardown body that calls `assert_*` is a gate that
# cannot fail.
#
# It also checks the wiring: a binary that uses the shared PHY mock must pass
# `test_tx_all_asserted` to `cmocka_run_group_tests()` (directly or from its own
# group teardown), otherwise its replies are never checked at all.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [ ! -d tests ]; then
    echo "FAIL: tests/ not found (run from the repository root)" >&2
    exit 1
fi

failed=0

# 1) No assertion inside a group teardown body.
while IFS=: read -r file line kind; do
    [ -z "$file" ] && continue
    echo "FAIL: $file:$line $kind uses a cmocka assertion; a group teardown cannot fail the process (use exit(EXIT_FAILURE))" >&2
    failed=1
done <<< "$(awk '
    FNR == 1 { in_body = 0 }
    !in_body && /^(static )?(int|void)[ \t]+(group_teardown|test_tx_all_asserted)[ \t]*\(/ {
        in_body = 1
        next
    }
    in_body && /^}/ {
        in_body = 0
        next
    }
    in_body && /[^A-Za-z_]assert_[a-z_]+[ \t]*\(/ {
        printf "%s:%d:%s\n", FILENAME, FNR, "group teardown"
    }
' tests/*.c)"

# 2) Every binary that includes the shared harness wires the teardown.
while IFS= read -r file; do
    [ -z "$file" ] && continue
    if ! grep -q 'test_tx_all_asserted' "$file"; then
        echo "FAIL: $file uses the shared PHY mock but never runs test_tx_all_asserted" >&2
        failed=1
    fi
done <<< "$(grep -l 'test_helpers\.h' tests/*.c 2>/dev/null | grep -v 'tests/test_helpers\.h' || true)"

if [ "$failed" -eq 0 ]; then
    echo "test gates: OK (group teardowns terminate, shared harness is wired)"
fi
exit "$failed"
