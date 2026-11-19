#!/usr/bin/env bash
#
# Differential frame gate: byte-exact transcripts across structural configs.
#
# Rebuilds the protocol test suite under every runnable structural
# configuration (tools/matrix_configs.sh is the single source of truth),
# records every device reply frame emitted through the mock PHY, and compares
# the transcripts byte for byte against the baseline configuration.
#
# Scope:
#   - the gate guards **structural equivalence** between the remaining
#     configurations (layout, concurrency tier, statistics mode);
#   - switches that became runtime capabilities (compat bits, system command,
#     wake-up, SIO C/Q, PD-input callback, ISR-tx-only) no longer produce a
#     configuration here. Their behaviour is covered by the expected-frame
#     tests in the C1 suite (tests/test_dll.c, tests/test_ds.c,
#     tests/test_system_commands.c). Structural equivalence and runtime
#     switches are complementary: neither replaces the other.
#
# A stale tools/diff_allowlist.txt entry (a configuration or transcript that no
# longer exists) is a hard failure, so a removed comparison cannot silently keep
# passing. Only BYTE/BYTE configurations take part: the gate compares reply
# transcripts, not transports (host_frame runs the unit suite but no transcript
# comparison).
#
# Build trees:
#   Each configuration is configured and built into IOLINKI_MATRIX_DIR/<cfg>, the
#   same tree tools/build_matrix.sh uses, so `tools/gates.sh matrix` does not
#   compile the same configuration twice. An existing tree is re-configured with
#   the row's declared CMAKE_C_FLAGS, BUILD_TESTING and test transport from
#   tools/matrix_configs.sh and rebuilt in place, so a transcript can only come
#   from binaries compiled with the parameters this gate declares. The run set
#   is what ctest registers for that tree, never a glob of the directory: a
#   reused tree keeps the binaries of tests this commit no longer has, and
#   running those would compare code that is not here. Each test runs under the
#   trace hook and a non-zero exit is red. ctest itself is not re-run on a reused
#   tree; the ctest-only entries (PTY conformance, IODD and generated-table
#   checks) belong to tools/build_matrix.sh, so a standalone run does not cover
#   them. A missing tree (the gate run on its own) is configured, built and
#   tested from scratch, and the gate prints that it built it instead of reusing
#   it. The two gates share the trees under IOLINKI_MATRIX_DIR, so they must not
#   run concurrently.
#
# Usage:
#   ./tools/diff_matrix.sh            # run the gate
#   ./tools/diff_matrix.sh --keep     # keep per-config matrix logs
#
# Environment:
#   IOLINKI_MATRIX_DIR build trees, shared with tools/build_matrix.sh
#                      (default: <repo>/build/matrix)
#   IOLINKI_DIFF_DIR   gate artifacts: per-config logs and transcripts
#                      (default: <repo>/build/diff)
#   JOBS               parallel build jobs (default: nproc)

set -uo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=tools/matrix_configs.sh
. "$ROOT/tools/matrix_configs.sh"
MATRIX_ROOT="${IOLINKI_MATRIX_DIR:-$ROOT/build/matrix}"
DIFF_ROOT="${IOLINKI_DIFF_DIR:-$ROOT/build/diff}"
TRACE_ROOT="$DIFF_ROOT/traces"
if [ -z "${JOBS:-}" ]; then
    JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi

KEEP=0

usage() {
    sed -n '3,47p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --keep) KEEP=1 ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 2 ;;
    esac
    shift
done

read -r -a CONFIGS <<<"$(iolink_matrix_runnable_configs)"
BASELINE="$(iolink_matrix_diff_baseline)"

if [ "${#CONFIGS[@]}" -eq 0 ]; then
    echo "diff gate: no runnable configuration in tools/matrix_configs.sh" >&2
    exit 1
fi

mkdir -p "$DIFF_ROOT"
STATUS=0

missing=()
for cfg in "${CONFIGS[@]}"; do
    [ -d "$MATRIX_ROOT/$cfg" ] || missing+=("$cfg")
done
if [ "${#missing[@]}" -gt 0 ]; then
    echo "diff gate: no build tree for ${missing[*]} in $MATRIX_ROOT; building instead of reusing (standalone run)"
fi

for cfg in "${CONFIGS[@]}"; do
    printf '%-18s ' "$cfg"
    keep_flag=()
    [ "$KEEP" -eq 1 ] && keep_flag=(--keep)
    origin="reused tree"
    reuse_flag=(--reuse-tree)
    if [ ! -d "$MATRIX_ROOT/$cfg" ]; then
        origin="built here"
        reuse_flag=()
    fi
    if ! IOLINKI_MATRIX_DIR="$MATRIX_ROOT" JOBS="$JOBS" \
        "$ROOT/tools/build_matrix.sh" --only "$cfg" "${reuse_flag[@]}" "${keep_flag[@]}" \
        >"$DIFF_ROOT/$cfg.matrix.log" 2>&1; then
        echo "BUILD/TEST FAILED"
        tail -20 "$DIFF_ROOT/$cfg.matrix.log" | sed 's/^/    /'
        STATUS=1
        continue
    fi

    rm -rf "${TRACE_ROOT:?}/$cfg"
    mkdir -p "$TRACE_ROOT/$cfg"
    # 测试失败必须让门禁变红：失败的二进制也可能会写出一份（空/半截）
    # transcript，静默忽略就会把"逐字节一致"变成空比较。
    # The run set comes from ctest's registration, not from a directory glob: a
    # reused tree keeps the binaries of tests that no longer exist, and running
    # those would compare transcripts of code this commit does not have.
    if ! (
        names="$(ctest --test-dir "$MATRIX_ROOT/$cfg" -N 2>/dev/null \
            | sed -nE 's/^[[:space:]]*Test[[:space:]]+#[[:space:]]*[0-9]+:[[:space:]]*//p' \
            | grep '^test_' || true)"
        if [ -z "$names" ]; then
            echo "    ctest lists no test_* target; cannot derive the transcript set" >&2
            exit 1
        fi
        cd "$MATRIX_ROOT/$cfg/tests" || exit 1
        while IFS= read -r test_bin; do
            [ -x "$test_bin" ] || {
                echo "    $test_bin is registered but not built" >&2
                exit 1
            }
            if ! IOLINK_DIFF_TRACE="$TRACE_ROOT/$cfg/$test_bin.txt" "./$test_bin" \
                    >/dev/null 2>&1; then
                echo "    $test_bin failed" >&2
                exit 1
            fi
        done <<< "$names"
    ); then
        echo "TEST FAILED"
        STATUS=1
        continue
    fi
    count="$(find "$TRACE_ROOT/$cfg" -name '*.txt' | wc -l)"
    echo "ok (${count} transcripts, ${origin})"
done

BASE="$TRACE_ROOT/$BASELINE"
if [ ! -d "$BASE" ]; then
    echo "diff gate: no baseline transcripts from '$BASELINE'" >&2
    exit 1
fi

ALLOWLIST="$ROOT/tools/diff_allowlist.txt"
ALLOWED_COUNT=0

# is_allowed <cfg> <transcript>: also reports stale entries as an error, so a
# removed configuration/test cannot keep skipping a comparison forever.
is_allowed() {
    local cfg="$1"
    local name="$2"
    local allowed_cfg allowed_test reason

    [ -f "$ALLOWLIST" ] || return 1
    while IFS='|' read -r allowed_cfg allowed_test reason; do
        case "$allowed_cfg" in
            ''|'#'*) continue ;;
        esac
        if [ -n "${reason:-}" ] && [ -z "$allowed_test" ]; then
            echo "diff gate: malformed allowlist entry for '$allowed_cfg' (no transcript)" >&2
            STATUS=1
        fi
        if [ "$allowed_cfg" = "$cfg" ] && [ "$allowed_test" = "$name" ]; then
            echo "    allowed: $cfg/$name ($reason)"
            return 0
        fi
    done <"$ALLOWLIST"
    return 1
}

# Staleness check: every entry must name a runnable configuration and a
# baseline transcript. Run before comparing so the failure is explicit.
if [ -f "$ALLOWLIST" ]; then
    while IFS='|' read -r allowed_cfg allowed_test reason; do
        case "$allowed_cfg" in
            ''|'#'*) continue ;;
        esac
        ALLOWED_COUNT=$((ALLOWED_COUNT + 1))
        if [ -z "${allowed_test:-}" ] || [ -z "${reason:-}" ]; then
            echo "diff gate: incomplete allowlist entry: $allowed_cfg|${allowed_test:-}|${reason:-}" >&2
            STATUS=1
            continue
        fi
        known_cfg=0
        for cfg in "${CONFIGS[@]:1}"; do
            [ "$cfg" = "$allowed_cfg" ] && known_cfg=1
        done
        if [ "$known_cfg" -eq 0 ]; then
            echo "diff gate: allowlist entry names a configuration that no longer runs: $allowed_cfg" >&2
            STATUS=1
            continue
        fi
        if [ ! -f "$BASE/$allowed_test" ]; then
            echo "diff gate: allowlist entry names an unknown transcript: $allowed_cfg/$allowed_test" >&2
            STATUS=1
        fi
    done <"$ALLOWLIST"
fi

for cfg in "${CONFIGS[@]:1}"; do
    for base_file in "$BASE"/*.txt; do
        name="$(basename "$base_file")"
        if is_allowed "$cfg" "$name"; then
            continue
        fi
        other="$TRACE_ROOT/$cfg/$name"
        if [ ! -f "$other" ]; then
            echo "DIFF $cfg/$name: transcript missing" >&2
            STATUS=1
            continue
        fi
        if ! cmp -s "$base_file" "$other"; then
            echo "DIFF $cfg/$name: reply frames differ from the baseline configuration" >&2
            cmp -l "$base_file" "$other" | head -3 | sed 's/^/    /' >&2
            STATUS=1
        fi
    done
done

if [ "$STATUS" -eq 0 ]; then
    echo "diff gate: PASS (all transcripts byte-identical across ${#CONFIGS[@]} configurations, ${ALLOWED_COUNT} allowlisted)"
else
    echo "diff gate: FAIL" >&2
fi
exit "$STATUS"
