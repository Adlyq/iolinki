/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_phy_diagnostics.c
 * @brief Unit tests for PHY diagnostics (voltage monitoring and short circuit detection)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdbool.h>

#include "iolinki/dll.h"
#include "iolinki/phy.h"
#include "iolinki/events.h"
#include "iolinki/time_utils.h"

/* Mock PHY implementation with configurable voltage and fault returns */
static int mock_voltage_mv = 24000; /* Default: 24V (normal) */
static bool mock_short_circuit = false;

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

static int mock_get_voltage_mv(void* user)
{
    (void) user;
    return mock_voltage_mv;
}

static bool mock_is_short_circuit(void* user)
{
    (void) user;
    return mock_short_circuit;
}

static const iolink_phy_api_t mock_phy = {.init = mock_init,
                                          .set_mode = mock_set_mode,
                                          .set_baudrate = mock_set_baudrate,
                                          .detect_wakeup = NULL,
                                          .set_cq_line = NULL,
                                          .get_voltage_mv = mock_get_voltage_mv,
                                          .is_short_circuit = mock_is_short_circuit};

/* PHY without diagnostics support */
static const iolink_phy_api_t mock_phy_no_diag = {.init = mock_init,
                                                  .set_mode = mock_set_mode,
                                                  .set_baudrate = mock_set_baudrate,
                                                  .detect_wakeup = NULL,
                                                  .set_cq_line = NULL,
                                                  .get_voltage_mv = NULL,
                                                  .is_short_circuit = NULL};

/* Mock PHY reporting physical faults through the preferred get_faults() hook. */
static uint8_t mock_faults = 0U;

static uint8_t mock_get_faults(void* user)
{
    (void) user;
    return mock_faults;
}

static const iolink_phy_api_t mock_phy_faults = {.init = mock_init,
                                                 .set_mode = mock_set_mode,
                                                 .set_baudrate = mock_set_baudrate,
                                                 .detect_wakeup = NULL,
                                                 .set_cq_line = NULL,
                                                 .get_faults = mock_get_faults};

static void test_voltage_monitoring_normal(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* 24V - normal */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* No voltage faults should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_voltage_monitoring_low(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 12000; /* 12V - too low */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Voltage fault should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* Event should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
}

static void test_voltage_monitoring_high(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 35000; /* 35V - too high */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Voltage fault should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* Event should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN);
}

static void test_voltage_monitoring_multiple_cycles(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 10000; /* 10V - too low */

    iolink_dll_init(&ctx, &mock_phy);

    /* Run multiple process cycles */
    for (int i = 0; i < 5; i++) {
        iolink_dll_process(&ctx);
    }

    /* Voltage faults should accumulate */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 5);
}

static void test_short_circuit_detection(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* Ensure normal voltage */
    mock_short_circuit = true;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Short circuit should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.short_circuits, 1);

    /* Event should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
}

static void test_short_circuit_no_fault(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* Ensure normal voltage */
    mock_short_circuit = false;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* No short circuit should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.short_circuits, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_phy_no_diagnostics_support(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* Use PHY without diagnostics support */
    iolink_dll_init(&ctx, &mock_phy_no_diag);
    iolink_dll_process(&ctx);

    /* Should not crash, counters should remain zero */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_int_equal(stats.short_circuits, 0);
}

static void test_combined_faults(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 10000; /* Too low */
    mock_short_circuit = true;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Both faults should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);
    assert_int_equal(stats.short_circuits, 1);

    /* Both events should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
}

static void test_voltage_unavailable_is_not_a_fault(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = -1; /* "measurement unavailable" is not a fault */
    mock_short_circuit = false;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_faults_default_mapping(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_faults = (uint8_t) (IOLINK_PHY_FAULT_UNDERVOLTAGE | IOLINK_PHY_FAULT_OVERTEMP |
                             IOLINK_PHY_FAULT_SHORT_CIRCUIT);

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_process(&ctx);

    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);
    assert_int_equal(stats.overtemp_faults, 1);
    assert_int_equal(stats.short_circuits, 1);

    /* Defaults use the standardized device codes (Table D.1): 0x5111, 0x4000, 0x7710. */
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_faults_edge_triggered(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;

    iolink_dll_init(&ctx, &mock_phy_faults);
    for (int i = 0; i < 5; i++) {
        iolink_dll_process(&ctx);
    }

    /* Counted every cycle, announced once: the Event flag stays set until the
       master reads the event memory, so the queue must not fill up. */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 5);
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_faults_appear_then_disappear(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_event_t ev;

    mock_faults = IOLINK_PHY_FAULT_SHORT_CIRCUIT;
    iolink_dll_init(&ctx, &mock_phy_faults);

    /* The fault starts: announced once, as "appears" (default mapping). */
    iolink_dll_process(&ctx);
    assert_true(iolink_events_flag(&ctx.events));
    assert_int_equal(iolink_events_memory_read(&ctx.events, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx.events, 0x01U), 0xF4U);
    /* Default short-circuit code is the standardized device code 0x7710. */
    assert_int_equal(iolink_events_memory_read(&ctx.events, 0x02U), 0x77U);
    assert_int_equal(iolink_events_memory_read(&ctx.events, 0x03U), 0x10U);

    /* Master confirms the readout (Table 60 T5). */
    iolink_events_memory_write(&ctx.events, 0x00U, 0x00U);
    assert_false(iolink_events_flag(&ctx.events));

    /* The fault ends, but the disappearance is held back for the A.6.4 minimum
       appear -> disappear window. */
    mock_faults = 0U;
    iolink_dll_process(&ctx);
    assert_false(iolink_events_flag(&ctx.events));

    /* Once the window has elapsed the disappearance is issued: the entry is
       removed from the event memory and the master is told to re-read. */
    uint32_t t0 = iolink_time_get_ms();
    while ((uint32_t) (iolink_time_get_ms() - t0) < IOLINK_EVENT_MIN_APPEAR_MS) {
        /* wait out the minimum appear -> disappear delay */
    }
    iolink_dll_process(&ctx);
    assert_true(iolink_events_flag(&ctx.events));
    assert_int_equal(iolink_events_memory_read(&ctx.events, 0x00U), 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx.events, 0x01U), 0x00U);
    assert_false(iolink_events_pending(&ctx.events));

    /* After the disappearance the same fault may be announced again. */
    mock_faults = IOLINK_PHY_FAULT_SHORT_CIRCUIT;
    iolink_dll_process(&ctx);
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
}

static void test_faults_st_mapping_override(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    /* The codes/severities ST's ministack application used (its IODD). */
    const iolink_dll_fault_map_t st_map = {
        .undervoltage = {0x1800U, IOLINK_EVENT_TYPE_ERROR},
        .overtemp = {0x1801U, IOLINK_EVENT_TYPE_WARNING},
        .short_circuit = {0x7710U, IOLINK_EVENT_TYPE_ERROR},
    };
    mock_faults = (uint8_t) (IOLINK_PHY_FAULT_UNDERVOLTAGE | IOLINK_PHY_FAULT_OVERTEMP |
                             IOLINK_PHY_FAULT_SHORT_CIRCUIT);

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_set_fault_map(&ctx, &st_map);
    iolink_dll_process(&ctx);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, 0x1800);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, 0x1801);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, 0x7710);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_faults_unreported_code(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    /* A code of 0 means: detect it, but do not report this fault. */
    const iolink_dll_fault_map_t map = {
        .undervoltage = {0U, IOLINK_EVENT_TYPE_WARNING},
        .overtemp = {IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD, IOLINK_EVENT_TYPE_ERROR},
        .short_circuit = {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR},
    };
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_set_fault_map(&ctx, &map);
    iolink_dll_process(&ctx);

    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);
    assert_false(iolink_events_pending(&ctx.events));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_voltage_monitoring_normal),
        cmocka_unit_test(test_voltage_monitoring_low),
        cmocka_unit_test(test_voltage_monitoring_high),
        cmocka_unit_test(test_voltage_monitoring_multiple_cycles),
        cmocka_unit_test(test_short_circuit_detection),
        cmocka_unit_test(test_short_circuit_no_fault),
        cmocka_unit_test(test_phy_no_diagnostics_support),
        cmocka_unit_test(test_combined_faults),
        cmocka_unit_test(test_voltage_unavailable_is_not_a_fault),
        cmocka_unit_test(test_faults_default_mapping),
        cmocka_unit_test(test_faults_edge_triggered),
        cmocka_unit_test(test_faults_appear_then_disappear),
        cmocka_unit_test(test_faults_st_mapping_override),
        cmocka_unit_test(test_faults_unreported_code),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
