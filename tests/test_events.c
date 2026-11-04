/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_events.c
 * @brief Unit tests for IO-Link Event handling
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <unistd.h>

#include "iolinki/events.h"

#include "conformance_test.h"
#include "test_helpers.h"

static void events_init(iolink_events_ctx_t* ctx)
{
    iolink_events_init(ctx);
    ctx->time_us = iolink_test_time_us;
}

static uint32_t g_fake_ms;

static uint64_t fake_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_fake_ms * 1000U;
}

static void test_event_queue_flow(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    assert_false(iolink_events_pending(&ctx));

    iolink_event_trigger(&ctx, 0x1234, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pending(&ctx));

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1234);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_false(iolink_events_pending(&ctx));
}

static void test_event_queue_overflow(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0075");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* Overfill the queue (IOLINK_EVENT_QUEUE_SIZE entries) with Notifications. */
    for (uint16_t i = 0U; i < (uint16_t) (IOLINK_EVENT_QUEUE_SIZE + 1U); i++) {
        iolink_event_trigger(&ctx, i, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
    const uint32_t drops_before = iolink_events_get_dropped_count(&ctx);
    assert_true(drops_before > 0U);

    /* A more severe event still overwrites the least severe one and is counted. */
    iolink_event_trigger(&ctx, 0xFFFF, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_get_dropped_count(&ctx) > drops_before);

    iolink_event_t ev;
    iolink_events_pop(&ctx, &ev); /* Skip first */
    /* Verify we can pop others */
    assert_true(iolink_events_pending(&ctx));
}

static void test_standard_codes(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, IOLINK_EVENT_COMM_TIMEOUT, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_CRC);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_TIMEOUT);
}

static void test_phy_diagnostic_codes(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    /* A.6.4: Errors are prioritized over Warnings. */
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);

    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
}

static void test_event_peek(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1122, IOLINK_EVENT_TYPE_NOTIFICATION);

    iolink_event_t ev;
    assert_true(iolink_events_peek(&ctx, &ev));
    assert_int_equal(ev.code, 0x1122);
    /* Should still be pending */
    assert_true(iolink_events_pending(&ctx));

    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1122);
    assert_false(iolink_events_pending(&ctx));
}

static void test_event_helpers(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* Test highest severity */
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0); /* OK */

    iolink_event_trigger(&ctx, 0x1001, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0); /* Still OK */

    iolink_event_trigger(&ctx, 0x1002, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4); /* Failure */

    iolink_event_trigger(&ctx, 0x1003, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4); /* Still Failure */

    /* Test get_all */
    iolink_event_t events[8];
    uint8_t count = iolink_events_get_all(&ctx, events, 8);
    assert_int_equal(count, 3);
    /* Severity order: Error(0x1002) > Warning(0x1003) > Notification(0x1001). */
    assert_int_equal(events[0].code, 0x1002);
    assert_int_equal(events[1].code, 0x1003);
    assert_int_equal(events[2].code, 0x1001);

    /* Test get_all with limit */
    count = iolink_events_get_all(&ctx, events, 2);
    assert_int_equal(count, 2);
}

static void test_event_classify(void** state)
{
    (void) state;
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_NO_MALFUNCTION),
                     IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST),
                     IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN),
                     IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_MAINTENANCE_REFILL),
                     IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_HARDWARE_FAULT),
                     IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_SHORT_CIRCUIT),
                     IOLINK_EVENT_TYPE_ERROR);
    /* Reserved/unknown defaults to error. */
    assert_int_equal(iolink_event_classify(0x2222U), IOLINK_EVENT_TYPE_ERROR);
}

static void test_event_memory_layout(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    assert_false(iolink_events_flag(&ctx));

    /* One error event: the memory freezes as the flag is raised (Table 60 T3). */
    iolink_event_trigger(&ctx, 0x1801U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    /* Table 58 / Figure A.22: StatusCode type 2, bit 7 set, bit 0 active. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Slot 1 at addresses 1,2,3: qualifier 0xF4 (appears|error|device|Application), 0x1801. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xF4U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x18U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x01U);

    /* A second event while frozen is queued but not visible. */
    iolink_event_trigger(&ctx, 0x1803U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0x00U);

    /* Reserved / out of range reads return 0. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x13U), 0x00U);
}

static void test_event_memory_frozen(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Further events while frozen are queued but not visible. */
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0x00U); /* slot 2 unused */
    assert_int_equal(ctx.count, 3U);                                 /* still queued */
}

static void test_event_memory_confirm(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    /* Table 60 T5: confirmation releases the presented slot; the event queued
       while frozen is re-frozen afterwards. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Confirming again clears the queue. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0x00U);

    /* Writes to other addresses are ignored. */
    iolink_event_trigger(&ctx, 0x4000U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&ctx));
    iolink_events_memory_write(&ctx, 0x01U, 0xAAU);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xE4U); /* warning qualifier */
}

/* Confirmation acknowledges only the slots that were actually presented; events
 * queued while frozen survive for the next readout. */
static void test_event_memory_confirm_only_presented(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    /* The first event freezes a 1-slot snapshot. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Further events arrive while frozen: queued but not yet presented. */
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 3U);

    /* Confirm: only the presented slot is acknowledged; the other two survive. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 2U);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x83U); /* 2 slots */
}

/* A.6.4: an Error/Warning appears once and disappears only after the 50 ms
 * minimum appearance window. */
static void test_event_appears_disappears(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0074");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, 0x1000U);

    /* Within the window the disappearance stays deferred. */
    iolink_events_process(&ctx);
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
    assert_false(iolink_events_pop(&ctx, &ev));

    usleep(60000U); /* > IOLINK_EVENT_MIN_APPEAR_MS */
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* The same EventCode may not appear twice while it is active; clearing an
 * unknown code is ignored. */
static void test_event_lifecycle_no_repeat(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0075");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ctx.count, 1U);

    iolink_event_clear(&ctx, 0x2000U); /* not active: ignored */
    assert_int_equal(ctx.count, 1U);
}

/* 10.10.2: no more than one report per second for the same EventCode. The
 * second incident is deferred, not dropped, and a timestamp of 0 is valid. */
static void test_event_recently_sent(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0076");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.time_us = fake_time_us;
    g_fake_ms = 0U;
    iolink_event_t ev;

    /* First appearance at millisecond 0 (the old sentinel dropped this). */
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    g_fake_ms = 50U;
    iolink_event_clear(&ctx, 0x1000U);
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);

    /* Re-raised inside the 1 s window: deferred, not queued or dropped. */
    g_fake_ms = 60U;
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 1U);

    /* Still inside the window at 999 ms. */
    g_fake_ms = 999U;
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 0U);

    /* At 1 s after the previous appearance the deferred event is emitted. */
    g_fake_ms = 1000U;
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
}

/* A full pending[] table must make the loss observable. */
static void test_event_pending_full_observable(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.time_us = fake_time_us;
    g_fake_ms = 0U;

    iolink_event_t ev;
    for (uint16_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_trigger(&ctx, (uint16_t) (0x2000U + i), IOLINK_EVENT_TYPE_ERROR);
        /* Drain the (smaller) event queue so pending[] is what fills up. */
        while (iolink_events_pop(&ctx, &ev)) {
        }
    }
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);

    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 1U);

    /* Mark one condition as gone and emit its withdrawal. */
    iolink_event_clear(&ctx, 0x2000U);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX - 1U);
    g_fake_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx);

    /* At the 1 s mark the disappears window is still open: the slot must not be
       recycled (that would erase its spacing history), so the surplus cause is
       dropped observably. */
    g_fake_ms = IOLINK_EVENT_MIN_REPEAT_MS;
    iolink_events_process(&ctx);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX - 1U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 2U);

    /* Both windows elapsed: the slot is released and reusable. */
    g_fake_ms = 2U * IOLINK_EVENT_MIN_REPEAT_MS;
    iolink_events_process(&ctx);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 2U);

    iolink_events_reset(&ctx);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
}

/* A.6.4: queued events are discarded when communication is interrupted. */
static void test_events_reset_clears(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    iolink_events_reset(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 0U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
}

static void test_event_flag_invariant(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* Empty queue: no flag. */
    assert_false(iolink_events_flag(&ctx));

    /* Draining the queue through pop() must clear the flag even though the
       event memory was never acknowledged. */
    iolink_event_trigger(&ctx, 0x1001U, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_true(iolink_events_flag(&ctx));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));

    /* Acknowledging the last Error through the event memory also clears it. */
    iolink_event_trigger(&ctx, 0x1002U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));
}

static void test_notification_dedup(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0075");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* A retrying Master must not turn repeated requests into an event storm:
       the same unacknowledged notification is queued only once. */
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 1U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0xFF91U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_SINGLE_SHOT);
    assert_false(iolink_events_pop(&ctx, &ev));
}

/* Table B.13: DeviceStatus reflects the current conditions, so it drops as
 * soon as the cause is gone, even while the "event disappears" is still
 * deferred by the 10.10.2 spacing. */
static void test_event_device_status_drops_with_condition(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.time_us = fake_time_us;
    g_fake_ms = 0U;

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4U);

    iolink_event_clear(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
    assert_int_equal(ctx.count, 1U); /* the appears is still queued */

    /* Inside the minimum appearance window the withdrawal is deferred, yet the
       status stays at OK because the condition is no longer asserted. */
    g_fake_ms = 10U;
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);

    /* A notification never raises DeviceStatus. */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_NO_MALFUNCTION, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
}

/* Table B.13: values and dominance. Priority 4->1, 3->2, 1->3, 2->4; the
 * lowest priority value wins, i.e. Failure > Functional-Check > Maintenance >
 * Out-of-Specification. */
static void test_event_device_status_matrix(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;

    const struct
    {
        uint16_t code;
        uint8_t expected;
    } single[] = {
        {IOLINK_EVENTCODE_MAINTENANCE_WEAR, 1U},   {IOLINK_EVENTCODE_MAINTENANCE_CLEANING, 1U},
        {IOLINK_EVENTCODE_MAINTENANCE_REFILL, 1U}, {IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 2U},
        {IOLINK_EVENTCODE_SIMULATION_ACTIVE, 3U},  {IOLINK_EVENTCODE_SHORT_CIRCUIT, 4U},
    };
    for (size_t i = 0U; i < (sizeof(single) / sizeof(single[0])); i++) {
        events_init(&ctx);
        iolink_event_trigger(&ctx, single[i].code, iolink_event_classify(single[i].code));
        assert_int_equal(iolink_events_get_highest_severity(&ctx), single[i].expected);
    }

    const struct
    {
        uint16_t first;
        uint16_t second;
        uint8_t expected;
    } pairs[] = {
        {IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 1U},
        {IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENTCODE_SIMULATION_ACTIVE, 3U},
        {IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, IOLINK_EVENTCODE_SIMULATION_ACTIVE, 3U},
        {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENTCODE_MAINTENANCE_WEAR, 4U},
        {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENTCODE_SIMULATION_ACTIVE, 4U},
        {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 4U},
        {IOLINK_EVENTCODE_SIMULATION_ACTIVE, IOLINK_EVENTCODE_SHORT_CIRCUIT, 4U},
    };
    for (size_t i = 0U; i < (sizeof(pairs) / sizeof(pairs[0])); i++) {
        events_init(&ctx);
        iolink_event_trigger(&ctx, pairs[i].first, iolink_event_classify(pairs[i].first));
        iolink_event_trigger(&ctx, pairs[i].second, iolink_event_classify(pairs[i].second));
        assert_int_equal(iolink_events_get_highest_severity(&ctx), pairs[i].expected);
    }
}

/* Table 101 "Clear": every asserted Error/Warning is withdrawn with MODE
 * "event disappears", queued Notifications are discarded, and DeviceStatus is
 * cleared by the reset. */
static void test_event_clear_diagnosis(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.time_us = fake_time_us;
    g_fake_ms = 0U;

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 3U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4U);

    iolink_events_clear_diagnosis(&ctx);

    /* The appearances already queued stay until acknowledged; the reset adds
       the matching disappearances immediately and drops the Notification
       (the queue is full at 4 entries, so a missing filter would silently
       evict the Notification and hide itself). */
    assert_int_equal(ctx.count, 4U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);

    /* Acknowledge the frozen appears snapshot: the withdraw pair becomes the
       presented set, so its MODE=2 qualifier is observable on the wire
       (Figure A.24: MODE=disappears, TYPE=error/warning, INSTANCE=Application
       -> 0xB4 / 0xA4). */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x87U); /* 3 slots */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xB4U); /* E disappears */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x10U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0xE4U); /* W appears */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x05U), 0x8CU);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x06U), 0x42U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x07U), 0xA4U); /* W disappears */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x08U), 0x8CU);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x09U), 0x42U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_MAINTENANCE_WEAR);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_MAINTENANCE_WEAR);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_int_equal(ctx.count, 0U);
}

/* A.6.4: an appearance that is still deferred by the 1 s window was never sent,
 * so Clear must not put an unpaired withdrawal on the wire. */
static void test_event_clear_diagnosis_skips_unreported_event(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.time_us = fake_time_us;
    g_fake_ms = 0U;

    iolink_event_t ev;
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_pop(&ctx, &ev)); /* the appearance reaches the master */
    iolink_event_clear(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    g_fake_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);

    /* Re-raised inside the 1 s window: the fresh appearance is deferred, the
       master has not seen it, so Clear has nothing to withdraw. */
    g_fake_ms = IOLINK_EVENT_MIN_REPEAT_MS / 2U;
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 0U);

    iolink_events_clear_diagnosis(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_queue_flow),
        cmocka_unit_test(test_event_queue_overflow),
        cmocka_unit_test(test_standard_codes),
        cmocka_unit_test(test_phy_diagnostic_codes),
        cmocka_unit_test(test_event_peek),
        cmocka_unit_test(test_event_helpers),
        cmocka_unit_test(test_event_device_status_drops_with_condition),
        cmocka_unit_test(test_event_device_status_matrix),
        cmocka_unit_test(test_event_classify),
        cmocka_unit_test(test_event_memory_layout),
        cmocka_unit_test(test_event_memory_frozen),
        cmocka_unit_test(test_event_memory_confirm),
        cmocka_unit_test(test_event_memory_confirm_only_presented),
        cmocka_unit_test(test_event_appears_disappears),
        cmocka_unit_test(test_event_lifecycle_no_repeat),
        cmocka_unit_test(test_event_recently_sent),
        cmocka_unit_test(test_event_pending_full_observable),
        cmocka_unit_test(test_events_reset_clears),
        cmocka_unit_test(test_event_flag_invariant),
        cmocka_unit_test(test_notification_dedup),
        cmocka_unit_test(test_event_clear_diagnosis),
        cmocka_unit_test(test_event_clear_diagnosis_skips_unreported_event),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
