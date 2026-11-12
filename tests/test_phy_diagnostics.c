/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_phy_diagnostics.c
 * @brief Unit tests for the PHY fault bitmask (get_faults) diagnostics.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdbool.h>

#include "iolinki/dll_ctx.h"
#include "iolinki/phy.h"
#include "iolinki/events.h"

static uint8_t mock_faults;

static int mock_init(void* user)
{
    (void) user;
    return 0;
}

static void mock_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void mock_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static uint8_t mock_get_faults(void* user)
{
    (void) user;
    return mock_faults;
}

static const iolink_phy_api_t mock_phy_faults = {
    .init = mock_init,
    .set_mode = mock_set_mode,
    .set_baudrate = mock_set_baudrate,
    .detect_wakeup = NULL,
    .get_faults = mock_get_faults,
};

/* PHY without a fault source: diagnostics stay silent. */
static const iolink_phy_api_t mock_phy_no_faults = {
    .init = mock_init,
    .set_mode = mock_set_mode,
    .set_baudrate = mock_set_baudrate,
    .detect_wakeup = NULL,
    .get_faults = NULL,
};

static void reset_fault_mock(void)
{
    mock_faults = 0U;
}

static void test_no_faults_hook_is_silent(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;

    reset_fault_mock();
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy_no_faults);
    iolink_dll_process(&ctx.main);

    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_int_equal(stats.overtemp_faults, 0);
    assert_int_equal(stats.short_circuits, 0);
    assert_false(iolink_events_pending(&ctx.main.shared.events));
}

/* The fault bitmask reports edges only: one count/event per 0->1 transition,
 * no repeats while the fault stays asserted, and a fall clears the event. */
static void test_get_faults_edges_only(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;

    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy_faults);
    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.main.shared.events, &ev));
    assert_int_equal(ev.code, 0x5111U);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ctx.main.shared.events.pending_count, 1U);

    /* Steady fault: no repeated count. */
    iolink_dll_process(&ctx.main);
    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* Fall: the pending condition is cleared (disappears is deferred to the
     * event engine, which needs the 50 ms window). */
    mock_faults = 0U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.shared.events.pending_count, 0U);

    /* A new rise counts again. */
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;
    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 2);
}

static void test_get_faults_overtemp_and_short(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = (uint8_t) (IOLINK_PHY_FAULT_OVERTEMP | IOLINK_PHY_FAULT_SHORT_CIRCUIT);

    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy_faults);
    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.overtemp_faults, 1);
    assert_int_equal(stats.short_circuits, 1);
    assert_int_equal(stats.voltage_faults, 0);

    /* Both errors are queued; the order within one severity is not asserted. */
    const uint16_t expected[2] = {IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD,
                                  IOLINK_EVENTCODE_SHORT_CIRCUIT};
    bool seen[2] = {false, false};
    for (int i = 0; i < 2; i++) {
        iolink_event_t ev;
        assert_true(iolink_events_pop(&ctx.main.shared.events, &ev));
        assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
        for (int k = 0; k < 2; k++) {
            if (ev.code == expected[k]) {
                seen[k] = true;
            }
        }
    }
    assert_true(seen[0]);
    assert_true(seen[1]);
    assert_false(iolink_events_pending(&ctx.main.shared.events));

    /* Partial fall: only the over-temp condition clears. Check the per-code
     * present flag, so clearing the wrong event cannot pass. */
    mock_faults = IOLINK_PHY_FAULT_SHORT_CIRCUIT;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.shared.events.pending_count, 1U);
    bool overtemp_present = false;
    bool short_present = false;
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (!ctx.main.shared.events.pending[i].active) {
            continue;
        }
        if (ctx.main.shared.events.pending[i].code == IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD) {
            overtemp_present = ctx.main.shared.events.pending[i].present;
        }
        if (ctx.main.shared.events.pending[i].code == IOLINK_EVENTCODE_SHORT_CIRCUIT) {
            short_present = ctx.main.shared.events.pending[i].present;
        }
    }
    assert_false(overtemp_present);
    assert_true(short_present);

    /* Full fall clears the remaining condition. */
    mock_faults = 0U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.shared.events.pending_count, 0U);
}

static uint32_t g_fault_ms;

static uint64_t fault_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_fault_ms * 1000U;
}

/* Clear -> disappears end to end: the falling edge must clear the mapped code,
 * and the event engine must emit its disappearance once the 50 ms window has
 * elapsed. A wrong mapping would clear a different code and leave this one
 * pending forever. */
static void test_get_faults_clear_emits_disappears(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_event_t ev;
    reset_fault_mock();
    g_fault_ms = 0U;
    mock_faults = IOLINK_PHY_FAULT_OVERTEMP;

    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy_faults);
    ctx.main.shared.time_us = fault_time_us;
    ctx.main.shared.events.time_us = fault_time_us;
    iolink_dll_process(&ctx.main);

    assert_true(iolink_events_pop(&ctx.main.shared.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    /* The fall is reported immediately, but the disappearance must respect the
     * 50 ms minimum window: nothing is queued at 10 ms. */
    g_fault_ms = 10U;
    mock_faults = 0U;
    iolink_dll_process(&ctx.main);
    iolink_events_process(&ctx.main.shared.events);
    assert_false(iolink_events_pending(&ctx.main.shared.events));

    g_fault_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx.main.shared.events);

    assert_true(iolink_events_pop(&ctx.main.shared.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

static void test_get_faults_overvoltage(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = IOLINK_PHY_FAULT_OVERVOLTAGE;

    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy_faults);
    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.main.shared.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);

    /* The falling edge clears the overvoltage condition. */
    assert_int_equal(ctx.main.shared.events.pending_count, 1U);
    mock_faults = 0U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.shared.events.pending_count, 0U);
}

/* A diagnostic reset drops the condition tracking; a fault that is still
 * present must be reported again in the new session (otherwise the master sees
 * a cleared DeviceStatus while the hardware is still faulted). */
static void test_get_faults_rearmed_after_reset(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;

    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy_faults);
    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 1);
    assert_int_equal(ctx.main.shared.events.pending_count, 1U);

    iolink_dll_reset_diagnostics(&ctx.main);
    assert_int_equal(ctx.main.shared.events.pending_count, 0U);

    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 2);
    assert_int_equal(ctx.main.shared.events.pending_count, 1U);

    /* The STARTUP path stages the same request as a flag (dll_set_state may run
     * in ISR context); consume it with a latch that still shows the fault. */
    ctx.main.phy_faults_last = IOLINK_PHY_FAULT_UNDERVOLTAGE;
    ctx.main.phy_faults_rearm = 1U;
    iolink_dll_process(&ctx.main);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.voltage_faults, 3);
    assert_int_equal(ctx.main.phy_faults_last, IOLINK_PHY_FAULT_UNDERVOLTAGE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_no_faults_hook_is_silent),
        cmocka_unit_test(test_get_faults_edges_only),
        cmocka_unit_test(test_get_faults_overtemp_and_short),
        cmocka_unit_test(test_get_faults_clear_emits_disappears),
        cmocka_unit_test(test_get_faults_overvoltage),
        cmocka_unit_test(test_get_faults_rearmed_after_reset),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
