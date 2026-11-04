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
 *
 * The A.6.4 lifecycle is timing based, so the tests inject a millisecond clock
 * instead of sleeping.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/events.h"

static uint32_t g_now_ms;

uint32_t iolink_time_get_ms(void)
{
    return g_now_ms;
}

uint64_t iolink_time_get_us(void)
{
    return (uint64_t) g_now_ms * 1000ULL;
}

static void clock_set(uint32_t ms)
{
    g_now_ms = ms;
}

static void assert_popped(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_mode_t mode)
{
    iolink_event_t ev;
    assert_true(iolink_events_pop(ctx, &ev));
    assert_int_equal(ev.code, code);
    assert_int_equal(ev.mode, mode);
}

static void test_event_queue_flow(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

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
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* Overfill the queue (IOLINK_EVENT_QUEUE_SIZE + 1 triggers) with Notifications. */
    for (uint16_t i = 0U; i < (uint16_t) (IOLINK_EVENT_QUEUE_SIZE + 1U); i++) {
        iolink_event_trigger(&ctx, i, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
    const uint32_t drops_before = iolink_events_get_dropped_count(&ctx);
    assert_true(drops_before > 0U);

    /* A more severe event still overwrites the least severe one and is counted. */
    iolink_event_trigger(&ctx, 0xFFFF, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_get_dropped_count(&ctx) > drops_before);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0xFFFFU);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);

    /* The least severe entry (the last Notification) was overwritten: the
       remaining queue holds the first QUEUE_SIZE-1 Notifications only. */
    uint8_t survivors = 0U;
    while (iolink_events_pop(&ctx, &ev)) {
        assert_true(ev.code < (IOLINK_EVENT_QUEUE_SIZE - 1U));
        assert_int_equal(ev.type, IOLINK_EVENT_TYPE_NOTIFICATION);
        survivors++;
    }
    assert_int_equal(survivors, (uint8_t) (IOLINK_EVENT_QUEUE_SIZE - 1U));
}

static void test_standard_codes(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

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
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

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
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

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
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

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
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

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
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
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
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
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
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
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

/* Regression: a more severe event queued while the memory is frozen is ordered
 * ahead of the presented entry. Confirmation must still acknowledge exactly the
 * presented entry, not the first N queue entries. */
static void test_event_memory_confirm_higher_severity_while_frozen(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* A Warning freezes a one-slot snapshot. */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xE4U); /* Warning appears */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x51U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x10U);

    /* An Error arrives while frozen and is ordered ahead of the Warning. */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 2U);
    assert_int_equal(ctx.queue[0].code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    /* The presented snapshot itself does not change. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xE4U);

    /* Confirming acknowledges the presented Warning, not the unseen Error. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(ctx.queue[0].code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xF4U); /* Error appears */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x77U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x10U);

    /* The surviving Error is still readable and acknowledgeable. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));
}

/* A.6.4: an Error/Warning appears once and disappears only after the 50 ms
 * minimum appearance window. */
static void test_event_appears_disappears(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);

    /* Within the window the disappearance stays deferred. */
    iolink_events_process(&ctx);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_APPEARS);
    assert_false(iolink_events_pending(&ctx));

    clock_set(IOLINK_EVENT_MIN_APPEAR_MS - 1U);
    iolink_events_process(&ctx);
    assert_false(iolink_events_pending(&ctx));

    /* At the 50 ms mark the disappearance is queued. */
    clock_set(IOLINK_EVENT_MIN_APPEAR_MS);
    iolink_events_process(&ctx);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* The same EventCode may not appear twice while it is active; clearing an
 * unknown code is ignored. */
static void test_event_lifecycle_no_repeat(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ctx.count, 1U);

    iolink_event_clear(&ctx, 0x2000U); /* not active: ignored */
    assert_int_equal(ctx.count, 1U);
}

/* Regression: the old "last_tx_ms != 0 means never sent" sentinel made a
 * timestamp of 0 special and silently dropped a re-raised event. The 1 s
 * same-EventQualifier spacing must defer, never lose, the new appearance. */
static void test_event_reappear_spacing_from_clock_zero(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* First incident at millisecond 0. */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    clock_set(IOLINK_EVENT_MIN_APPEAR_MS);
    iolink_events_process(&ctx);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_APPEARS);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_DISAPPEARS);

    /* Re-raised inside the 1 s window: deferred, not dropped. */
    clock_set(500U);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 1U);

    /* Still inside the window at 999 ms. */
    clock_set(IOLINK_EVENT_MIN_REPEAT_MS - 1U);
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 0U);

    /* At 1 s after the previous appearance the new one is queued. */
    clock_set(IOLINK_EVENT_MIN_REPEAT_MS);
    iolink_events_process(&ctx);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_APPEARS);
}

/* A deferred appearance that disappears before it could be reported needs no
 * withdrawal: the Master never saw the event. */
static void test_event_deferred_appear_cleared(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    clock_set(IOLINK_EVENT_MIN_APPEAR_MS);
    iolink_events_process(&ctx);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_APPEARS);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_DISAPPEARS);

    clock_set(500U);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(
        &ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION); /* gone before the 1 s spacing elapsed */

    clock_set(2000U);
    iolink_events_process(&ctx);
    assert_false(iolink_events_pending(&ctx));
    assert_int_equal(ctx.pending_count, 0U);
}

/* 10.10.2: the 1 s spacing applies to each EventCode/EventQualifier pair, so a
 * rapidly cycling code may not report two "disappears" inside one second even
 * when its "appears" pair is already 1 s old. */
static void test_event_disappears_repeat_spacing(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    clock_set(IOLINK_EVENT_MIN_REPEAT_MS);
    iolink_events_process(&ctx);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_APPEARS);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_DISAPPEARS);

    /* New incident at the same instant: "appears" is allowed (1 s after the
       previous "appears"), "disappears" is not (1 s after the last one). */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_APPEARS);
    iolink_event_clear(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);

    clock_set(IOLINK_EVENT_MIN_REPEAT_MS + IOLINK_EVENT_MIN_APPEAR_MS);
    iolink_events_process(&ctx);
    assert_false(iolink_events_pending(&ctx));

    clock_set(2U * IOLINK_EVENT_MIN_REPEAT_MS - 1U);
    iolink_events_process(&ctx);
    assert_false(iolink_events_pending(&ctx));

    clock_set(2U * IOLINK_EVENT_MIN_REPEAT_MS);
    iolink_events_process(&ctx);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* When the lifecycle table is full the surplus cause is dropped, but the drop
 * is observable and the slot becomes reusable after the 1 s cooldown. */
static void test_event_pending_full_observable(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_t ev;
    for (uint16_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_trigger(&ctx, (uint16_t) (0x5000U + i), IOLINK_EVENT_TYPE_ERROR);
        /* Drain the (smaller) event queue so pending[] is what fills up. */
        while (iolink_events_pop(&ctx, &ev)) {
        }
    }
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);

    /* The 9th distinct cause cannot be tracked; the counter reports the loss. */
    iolink_event_trigger(&ctx, 0x6000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 1U);

    /* Free one slot: emit its disappear, then wait until both 1 s windows have
       elapsed before the slot can be reused. */
    iolink_event_clear(&ctx, 0x5000U);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX - 1U);
    clock_set(IOLINK_EVENT_MIN_APPEAR_MS);
    iolink_events_process(&ctx);

    /* At the 1 s mark the disappears window is still open: the slot must not
       be recycled (which would erase its spacing history) and the new cause is
       dropped, observably. */
    clock_set(IOLINK_EVENT_MIN_REPEAT_MS);
    iolink_events_process(&ctx);
    iolink_event_trigger(&ctx, 0x6000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX - 1U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 2U);

    /* Both windows elapsed: the slot is released and reusable. */
    clock_set(2U * IOLINK_EVENT_MIN_REPEAT_MS);
    iolink_events_process(&ctx);
    iolink_event_trigger(&ctx, 0x6000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 2U);

    bool found = false;
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (ctx.pending[i].active && ctx.pending[i].present && (ctx.pending[i].code == 0x6000U)) {
            found = true;
        }
    }
    assert_true(found);

    /* A reset also clears the diagnostic counter. */
    iolink_events_reset(&ctx);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
}

/* A.6.4: queued events are discarded when communication is interrupted. */
static void test_events_reset_clears(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    iolink_events_reset(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 0U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);

    /* 10.10.2: after communication resumes the application reports the current
       causes again, so the same code must produce a fresh appearance instead of
       being treated as still asserted. */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, 1U);
    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_APPEARS);
}

/* The Event flag must satisfy flag == (count > 0) at every setter, so a Master
 * is never pointed at an empty diagnosis queue. */
static void test_event_flag_invariant(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* Empty queue: no flag. The stored bit is checked directly because
       iolink_events_flag() already masks with count > 0 and would hide a
       setter that leaves the bit set on an empty queue. */
    assert_false(ctx.flag);
    assert_int_equal(ctx.flag, (ctx.count > 0U));

    /* Draining the queue through pop() must clear the flag even though the
       event memory was never acknowledged. */
    iolink_event_trigger(&ctx, 0x1001U, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_true(ctx.flag);
    assert_int_equal(ctx.flag, (ctx.count > 0U));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ctx.count, 0U);
    assert_false(ctx.flag);
    assert_int_equal(ctx.flag, (ctx.count > 0U));

    /* Acknowledging the last Error through the event memory also clears it. */
    iolink_event_trigger(&ctx, 0x1002U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(ctx.flag);
    assert_int_equal(ctx.flag, (ctx.count > 0U));
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 0U);
    assert_false(ctx.flag);
    assert_int_equal(ctx.flag, (ctx.count > 0U));
}

/* A retrying Master must not turn repeated requests into an event storm: the
 * same unacknowledged notification is queued only once. */
static void test_notification_dedup(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 1U);

    assert_popped(&ctx, 0xFF91U, IOLINK_EVENT_MODE_SINGLE_SHOT);
    assert_false(iolink_events_pending(&ctx));

    /* Once acknowledged, the same notification may be reported again. */
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 1U);
    assert_popped(&ctx, 0xFF91U, IOLINK_EVENT_MODE_SINGLE_SHOT);
}

/* 10.10.2/Table A.20: Notifications are always single shot (MODE = 1), never
 * "appears" (MODE = 3). */
static void test_event_notification_mode_single_shot(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);

    /* MODE 01 (bits 6-7) | TYPE Notification 01 (bits 5-4) | INSTANCE 4. */
    assert_int_equal(iolink_events_memory_read(&ctx, 1U), 0x54U);
    /* 0xFF91 is still presented to the Master in the Table 58 event memory. */
    assert_int_equal(iolink_events_memory_read(&ctx, 2U), 0xFFU);
    assert_int_equal(iolink_events_memory_read(&ctx, 3U), 0x91U);
}

/* Table 101 "Clear": every asserted Error/Warning is withdrawn with MODE
 * "event disappears", queued Notifications are discarded, and DeviceStatus is
 * cleared by the reset. */
static void test_event_clear_diagnosis(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 3U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4U);

    iolink_events_clear_diagnosis(&ctx);

    /* The appearances already queued stay until acknowledged; the reset adds
       the matching disappearances immediately and drops the Notification. */
    assert_int_equal(ctx.count, 4U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);

    /* Acknowledge the frozen appears snapshot: the withdraw pair becomes the
       presented set, so its MODE=2 qualifier is observable on the wire
       (Table A.20: MODE 10, TYPE 11/10, INSTANCE 4 -> 0xB4 / 0xA4). */
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

    assert_popped(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_popped(&ctx, IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENT_MODE_APPEARS);
    assert_popped(&ctx, IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_pending(&ctx));
}

static void test_event_clear_diagnosis_discards_all_notifications(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_NO_MALFUNCTION, IOLINK_EVENT_TYPE_NOTIFICATION);
    iolink_events_clear_diagnosis(&ctx);

    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
}

/* Table D.1: DS_UPLOAD_REQ is an internal Notification, not visible to the
 * user, with Preferred DeviceStatus "0". */
static void test_event_ds_upload_request_invisible_in_device_status(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);

    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
    /* Still queued for the Master; only the DeviceStatus is unaffected. */
    assert_true(iolink_events_pending(&ctx));
}

/* Table B.13/Table D.1: a notification never changes DeviceStatus, including
 * the standardized "No malfunction" (0x0000) notification. */
static void test_event_no_malfunction_notification_invisible_in_device_status(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_NO_MALFUNCTION, IOLINK_EVENT_TYPE_NOTIFICATION);

    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
    assert_true(iolink_events_pending(&ctx));
}

/* Table B.13: DeviceStatus values and their dominance. Each value has a
 * priority (0 OK = 5, 1 Maintenance = 3, 2 Out-of-Spec = 4, 3 Functional = 2,
 * 4 Failure = 1) and the lowest priority value wins, i.e.
 * Failure > Functional-Check > Maintenance-Required > Out-of-Specification. */
static void test_event_device_status_matrix(void** state)
{
    (void) state;
    clock_set(0U);
    iolink_events_ctx_t ctx;

    const struct
    {
        uint16_t code;
        uint8_t expected;
    } single[] = {
        {IOLINK_EVENTCODE_MAINTENANCE_WEAR, 1U},
        {IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 2U},
        {IOLINK_EVENTCODE_SIMULATION_ACTIVE, 3U},
        {IOLINK_EVENTCODE_SHORT_CIRCUIT, 4U},
    };
    for (size_t i = 0U; i < (sizeof(single) / sizeof(single[0])); i++) {
        iolink_events_init(&ctx);
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
        iolink_events_init(&ctx);
        iolink_event_trigger(&ctx, pairs[i].first, iolink_event_classify(pairs[i].first));
        iolink_event_trigger(&ctx, pairs[i].second, iolink_event_classify(pairs[i].second));
        assert_int_equal(iolink_events_get_highest_severity(&ctx), pairs[i].expected);
    }
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
        cmocka_unit_test(test_event_device_status_matrix),
        cmocka_unit_test(test_event_classify),
        cmocka_unit_test(test_event_memory_layout),
        cmocka_unit_test(test_event_memory_frozen),
        cmocka_unit_test(test_event_memory_confirm),
        cmocka_unit_test(test_event_memory_confirm_only_presented),
        cmocka_unit_test(test_event_memory_confirm_higher_severity_while_frozen),
        cmocka_unit_test(test_event_appears_disappears),
        cmocka_unit_test(test_event_lifecycle_no_repeat),
        cmocka_unit_test(test_event_reappear_spacing_from_clock_zero),
        cmocka_unit_test(test_event_deferred_appear_cleared),
        cmocka_unit_test(test_event_disappears_repeat_spacing),
        cmocka_unit_test(test_event_pending_full_observable),
        cmocka_unit_test(test_events_reset_clears),
        cmocka_unit_test(test_event_flag_invariant),
        cmocka_unit_test(test_notification_dedup),
        cmocka_unit_test(test_event_notification_mode_single_shot),
        cmocka_unit_test(test_event_clear_diagnosis),
        cmocka_unit_test(test_event_clear_diagnosis_discards_all_notifications),
        cmocka_unit_test(test_event_ds_upload_request_invisible_in_device_status),
        cmocka_unit_test(test_event_no_malfunction_notification_invisible_in_device_status),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
