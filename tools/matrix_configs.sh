#!/usr/bin/env bash
#
# Single source of truth for the structural configuration matrix.
#
# Both tools/build_matrix.sh and tools/diff_matrix.sh source this file. A
# configuration exists exactly once here; the differential gate reads the
# runnable subset and fails on a stale allowlist entry instead of silently
# skipping a comparison that no longer exists.
#
# The matrix is **structure x platform**, not one entry per switch:
#   C1 host_default   BYTE/BYTE, ISR + COMPILER fence, SHARED stats, full stack
#                     (plus the Release build-type variant)
#   C2 host_frame     FRAME/FRAME, SHARED stats, full stack
#   C3 host_no_ds     BYTE/BYTE, full stack minus Data Storage
#   C4 host_no_events BYTE/BYTE, full stack minus the event engine
#   C5 target_minimal BYTE/BYTE, SINGLE (no critical-section symbols), no DS,
#                     no events, BAREMETAL platform, Release
#   C6 host_smp_split BYTE/BYTE, SMP + SMP fence, SPLIT stats, full stack
#
# C1, C2, C3, C4 and C6 run tests; C3/C4 register only the trimmed-behaviour
# contract. C2 passes -DIOLINK_TEST_TRANSPORT=FRAME
# (build_matrix.sh derives it from the mode), which drops the targets that pin
# a BYTE transport contract; see docs/TESTING.md. C2 carries no
# IOLINK_PHY_FEATURE_TX_BUSY=1 because that build requires every PHY under test
# to provide tx_busy(); that layout is compiled by the tx_busy_lease probe and
# exercised by the test_tx_busy fixture.
#
# Everything that is no longer a structural difference (compat bits, system
# command, wake-up, SIO C/Q, PD-input callback, ISR-tx-only) is a runtime
# capability and is covered by expected-frame tests in the C1 suite, not by a
# configuration here. The probe list below covers the legal-but-uninstantiated
# combinations.

# Structural configuration names (what --list reports as the matrix size).
# shellcheck disable=SC2034 # consumed by the scripts that source this file
IOLINK_MATRIX_STRUCTURAL_CONFIGS=(
    host_default host_frame host_no_ds host_no_events target_minimal host_smp_split)

# Emit every build row: <callback> <name> <mode> <build-type> <tests> <cmake-extra> <cflags-extra>
#
# host_default_release is the Release build-type variant of C1: it does not add
# a structural configuration, it only exercises the optimisation level.
iolink_matrix_emit()
{
    local cb="$1"
    "$cb" host_default BYTE_BYTE Debug yes "" ""
    "$cb" host_default_release BYTE_BYTE Release yes "" ""
    "$cb" host_frame FRAME_FRAME Debug yes "" ""
    # Trimmed builds run only tests/test_trimmed_contract.c, which pins the
    # responses the master can observe when a subsystem is compiled out.
    "$cb" host_no_ds BYTE_BYTE Debug yes "-DIOLINK_FEATURE_DATA_STORAGE=OFF" ""
    "$cb" host_no_events BYTE_BYTE Debug yes "-DIOLINK_FEATURE_EVENTS=OFF" ""
    "$cb" target_minimal BYTE_BYTE Release no \
        "-DIOLINK_FEATURE_DATA_STORAGE=OFF -DIOLINK_FEATURE_EVENTS=OFF -DIOLINK_PLATFORM=BAREMETAL" \
        "-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_SINGLE"
    "$cb" host_smp_split BYTE_BYTE Debug yes \
        "-DIOLINK_STATS_MODE=IOLINK_STATS_SPLIT" \
        "-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_SMP -DIOLINK_FENCE_LEVEL=IOLINK_FENCE_SMP -DIOLINK_FENCE_SMP_IMPL=__sync_synchronize"
}

# Configurations the differential gate rebuilds. It compares protocol reply
# transcripts, so only BYTE/BYTE configurations take part; host_frame runs the
# unit suite but not the transcript comparison.
iolink_matrix_runnable_configs()
{
    echo host_default host_smp_split
}

# Differential-gate baseline (must be one of the runnable configurations).
iolink_matrix_diff_baseline()
{
    echo host_default
}

# Configuration that must not reference iolink_critical_* (SINGLE profile).
iolink_matrix_no_critical_config()
{
    echo target_minimal
}

# Compile-only combination probes.
#
# These combinations stay legal in the preprocessor but are not structural
# configurations, so they must at least keep compiling: they only need
# cc -fsyntax-only over the core sources, not a linked image. A probe failure
# is a red CI result, never a "note".
#
# <callback> <name> <cflags>
iolink_matrix_emit_probes()
{
    local cb="$1"
    "$cb" isr_frame_byte "-DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_BYTE"
    "$cb" isr_byte_frame "-DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_BYTE -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME"
    "$cb" frame_three_buffers "-DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME"
    "$cb" tx_busy_lease "-DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME -DIOLINK_PHY_FEATURE_TX_BUSY=1"
    "$cb" no_data_storage "-DIOLINK_FEATURE_DATA_STORAGE=0"
    "$cb" no_events "-DIOLINK_FEATURE_EVENTS=0"
    "$cb" no_validate "-DIOLINK_FEATURE_VALIDATE=0"
    "$cb" isr_log "-DIOLINK_LOG_ENABLE_ISR=1"
    "$cb" workspace_external "-DIOLINK_WORKSPACE_EMBEDDED=0"
    "$cb" single_no_critical "-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_SINGLE"
    "$cb" capacity_overrides "-DIOLINK_MBX_SLOTS=8 -DIOLINK_REPLY_PLAN_SLOTS=12"
}
