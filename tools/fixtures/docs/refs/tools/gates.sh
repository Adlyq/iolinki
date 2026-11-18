#!/bin/sh
# 只引用存在的仓库路径；变量、URL、/tmp 与 build/ 不在检查范围。
set -eu
cat src/fixture.c >/dev/null
cat docs/good.md >/dev/null
if [ -n "${FIXTURE_ROOT:-}" ]; then
    cat "$FIXTURE_ROOT/tools/ignored.sh" >/dev/null 2>&1 || true
fi
curl -fsSL https://example.com/docs/remote.md >/dev/null 2>&1 || true
ls /tmp/fixture-output.txt >/dev/null 2>&1 || true
ls build/generated.md >/dev/null 2>&1 || true
cat docs/good.md. >/dev/null 2>&1 || true
for unit in src/fixture*.c; do :; done
