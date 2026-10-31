/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file events.c
 * @brief IO-Link Event Handling
 * @ingroup iolinki_events
 *
 * Implements the device event queue (enqueue/pop/peek), severity ranking and
 * event-code classification used to report diagnostics to the master.
 */

#include "iolinki/events.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include <string.h>

#if IOLINK_FEATURE_EVENTS

void iolink_events_init(iolink_events_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_events_ctx_t))) {
        return;
    }
}

/** @brief Current time in milliseconds from the bound application clock. */
static uint32_t events_now_ms(const iolink_events_ctx_t* ctx)
{
    if (ctx->time_us == NULL) {
        return 0U;
    }
    return (uint32_t) (ctx->time_us(ctx->time_user) / 1000U);
}

/** @brief EventQualifier for an event type and MODE (A.6.4, Figure A.24). */
static uint8_t event_qualifier(iolink_event_type_t type, iolink_event_mode_t mode)
{
    uint8_t q;
    switch (mode) {
        case IOLINK_EVENT_MODE_DISAPPEARS:
            q = 0x80U; /* MODE = event disappears (0b10) */
            break;
        case IOLINK_EVENT_MODE_SINGLE_SHOT:
            q = 0x40U; /* MODE = event single shot (0b01) */
            break;
        default:
            q = 0xC0U; /* MODE = event appears (0b11) */
            break;
    }
    switch (type) {
        case IOLINK_EVENT_TYPE_NOTIFICATION:
            q |= (0x01U << 4);
            break;
        case IOLINK_EVENT_TYPE_WARNING:
            q |= (0x02U << 4);
            break;
        case IOLINK_EVENT_TYPE_ERROR:
            q |= (0x03U << 4);
            break;
        default:
            break;
    }
    q |= 0x04U; /* SOURCE = device (bit 3 = 0), INSTANCE = Application (Table A.17) */
    return q;
}

/** @brief Rebuild the Table 58 event memory from the oldest queued events.
 *
 * Up to six slots are populated; the StatusCode carries a bit per active slot.
 * The caller holds the critical section. Returns the number of slots written. */
static uint8_t event_memory_rebuild(iolink_events_ctx_t* ctx)
{
    (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    uint8_t slots = ctx->count;
    if (slots > 6U) {
        slots = 6U;
    }
    for (uint8_t i = 0U; i < slots; i++) {
        const iolink_event_t* ev = &ctx->queue[i];
        ctx->memory[1U + i * 3U] = event_qualifier(ev->type, ev->mode);
        ctx->memory[2U + i * 3U] = (uint8_t) (ev->code >> 8);
        ctx->memory[3U + i * 3U] = (uint8_t) (ev->code & 0xFFU);
    }
    /* StatusCode type 2: Event Details = 1, bits 0-5 = one bit per active slot
       (Figure A.22/A.23: bit 0 is Event 1, bit 1 is Event 2, ...). */
    uint8_t active = 0U;
    for (uint8_t i = 0U; i < slots; i++) {
        active |= (uint8_t) (1U << i);
    }
    ctx->memory[0] = (uint8_t) (0x80U | active);
    ctx->frozen_count = slots;
    for (uint8_t i = 0U; i < slots; i++) {
        ctx->frozen_events[i] = ctx->queue[i];
    }
    return slots;
}

/** @brief Insert an event into the severity-ordered queue (Errors first). */
static void event_queue_insert(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type,
                               iolink_event_mode_t mode)
{
    uint8_t pos = 0U;
    while ((pos < ctx->count) && (ctx->queue[pos].type >= type)) {
        pos++;
    }
    if (ctx->count >= IOLINK_EVENT_QUEUE_SIZE) {
        if (pos >= IOLINK_EVENT_QUEUE_SIZE) {
            return; /* Queue holds more severe events: drop the new one. */
        }
        ctx->count = (uint8_t) (IOLINK_EVENT_QUEUE_SIZE - 1U);
    }
    for (uint8_t i = ctx->count; i > pos; i--) {
        ctx->queue[i] = ctx->queue[i - 1U];
    }
    ctx->queue[pos].code = code;
    ctx->queue[pos].type = type;
    ctx->queue[pos].mode = mode;
    ctx->count++;
}

static iolink_event_pending_t* pending_find(iolink_events_ctx_t* ctx, uint16_t code)
{
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (ctx->pending[i].active && (ctx->pending[i].code == code)) {
            return &ctx->pending[i];
        }
    }
    return NULL;
}

static iolink_event_pending_t* pending_alloc(iolink_events_ctx_t* ctx)
{
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (!ctx->pending[i].active) {
            return &ctx->pending[i];
        }
    }
    return NULL;
}

/** @brief A.6.4: the same EventCode is not reported more than once per second. */
static bool event_recently_sent(const iolink_events_ctx_t* ctx, uint16_t code, uint32_t now)
{
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        const iolink_event_pending_t* p = &ctx->pending[i];
        if ((p->code == code) && (p->last_tx_ms != 0U) &&
            ((now - p->last_tx_ms) < IOLINK_EVENT_MIN_REPEAT_MS)) {
            return true;
        }
    }
    return false;
}

/** @brief Freeze the memory and raise the flag (Table 60 T3).
 *
 * The flag must never be set while the queue is empty: a set flag tells the
 * Master to read the diagnosis channel, and an empty queue there produces a
 * read/acknowledge loop. */
static void event_publish(iolink_events_ctx_t* ctx)
{
    if (!ctx->frozen && (ctx->count > 0U)) {
        ctx->frozen_slots = event_memory_rebuild(ctx);
        ctx->frozen = true;
    }
    ctx->flag = (ctx->count > 0U);
}

/** @brief Whether @p code is already queued (any MODE). */
static bool event_code_queued(const iolink_events_ctx_t* ctx, uint16_t code)
{
    for (uint8_t i = 0U; i < ctx->count; i++) {
        if (ctx->queue[i].code == code) {
            return true;
        }
    }
    return false;
}

void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type)
{
    if (ctx == NULL) {
        return;
    }

    IOLINK_CRITICAL_ENTER();

    const uint32_t now = events_now_ms(ctx);
    if (event_recently_sent(ctx, code, now)) {
        IOLINK_CRITICAL_EXIT();
        return; /* A.6.4: no more than one report per second per EventCode. */
    }

    if (type == IOLINK_EVENT_TYPE_NOTIFICATION) {
        /* Notifications are single-shot. Do not queue the same code again while
           the previous instance is still unacknowledged: a retrying Master must
           not turn repeated requests into an event storm. */
        if (!event_code_queued(ctx, code)) {
            event_queue_insert(ctx, code, type, IOLINK_EVENT_MODE_SINGLE_SHOT);
        }
    }
    else {
        iolink_event_pending_t* p = pending_find(ctx, code);
        if (p == NULL) {
            p = pending_alloc(ctx);
        }
        if ((p != NULL) && !p->active) {
            p->code = code;
            p->type = type;
            p->appear_ms = now;
            p->last_tx_ms = now;
            p->clear_pending = false;
            p->active = true;
            ctx->pending_count++;
            event_queue_insert(ctx, code, type, IOLINK_EVENT_MODE_APPEARS);
        }
        /* An already-active code may not appear again until it disappears. */
    }

    event_publish(ctx);
    IOLINK_CRITICAL_EXIT();
}

void iolink_event_clear(iolink_events_ctx_t* ctx, uint16_t code)
{
    if (ctx == NULL) {
        return;
    }

    IOLINK_CRITICAL_ENTER();
    iolink_event_pending_t* p = pending_find(ctx, code);
    if (p != NULL) {
        p->clear_pending = true; /* emitted by iolink_events_process() */
    }
    IOLINK_CRITICAL_EXIT();
}

void iolink_events_process(iolink_events_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    IOLINK_CRITICAL_ENTER();
    const uint32_t now = events_now_ms(ctx);
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_pending_t* p = &ctx->pending[i];
        if (!p->active || !p->clear_pending) {
            continue;
        }
        if ((now - p->appear_ms) < IOLINK_EVENT_MIN_APPEAR_MS) {
            continue; /* Still within the minimum appearance window. */
        }
        p->clear_pending = false;
        p->active = false;
        p->last_tx_ms = now;
        if (ctx->pending_count > 0U) {
            ctx->pending_count--;
        }
        event_queue_insert(ctx, p->code, p->type, IOLINK_EVENT_MODE_DISAPPEARS);
        event_publish(ctx);
    }
    IOLINK_CRITICAL_EXIT();
}

void iolink_events_reset(iolink_events_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    IOLINK_CRITICAL_ENTER();
    ctx->count = 0U;
    ctx->pending_count = 0U;
    (void) memset(ctx->pending, 0, sizeof(ctx->pending));
    (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    ctx->frozen = false;
    ctx->flag = false;
    ctx->frozen_slots = 0U;
    IOLINK_CRITICAL_EXIT();
}

bool iolink_events_flag(const iolink_events_ctx_t* ctx)
{
    /* Never advertise an Event flag with an empty queue. The setters keep
       flag == (count > 0); this guard also covers a concurrent ISR read. */
    return ((ctx != NULL) && ctx->flag && (ctx->count > 0U));
}

uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr)
{
    if ((ctx == NULL) || (addr >= sizeof(ctx->memory))) {
        return 0U;
    }
    IOLINK_CRITICAL_ENTER();
    /* The memory was frozen when the Event flag was raised (Table 60 T3), so a
       readout is always a consistent snapshot. */
    const uint8_t value = ctx->memory[addr];
    IOLINK_CRITICAL_EXIT();
    return value;
}

static void event_queue_remove(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_mode_t mode)
{
    for (uint8_t i = 0U; i < ctx->count; i++) {
        if ((ctx->queue[i].code == code) && (ctx->queue[i].mode == mode)) {
            for (uint8_t j = i; (uint8_t) (j + 1U) < ctx->count; j++) {
                ctx->queue[j] = ctx->queue[j + 1U];
            }
            ctx->count--;
            return;
        }
    }
}

void iolink_events_memory_write(iolink_events_ctx_t* ctx, uint8_t addr, uint8_t value)
{
    (void) value;
    if ((ctx == NULL) || (addr != 0U)) {
        return;
    }

    IOLINK_CRITICAL_ENTER();
    /* Table 60 T5: confirmation releases the memory and clears the flag. Only
       the slots actually presented (frozen_slots) are acknowledged; events
       queued while frozen are rebuilt below and become visible again. */
    ctx->frozen = false;
    ctx->flag = false;
    if (ctx->frozen_slots > 0U) {
        /* Acknowledge exactly the events that were presented in the frozen
           memory; higher-severity events queued after the freeze must remain. */
        const uint8_t acked =
            (ctx->frozen_count < ctx->frozen_slots) ? ctx->frozen_count : ctx->frozen_slots;
        for (uint8_t i = 0U; i < acked; i++) {
            event_queue_remove(ctx, ctx->frozen_events[i].code, ctx->frozen_events[i].mode);
        }
    }
    else {
        uint8_t acked = ctx->count;
        if (acked > 6U) {
            acked = 6U;
        }
        for (uint8_t i = 0U; i < acked; i++) {
            if (ctx->count > 0U) {
                for (uint8_t j = 0U; (uint8_t) (j + 1U) < ctx->count; j++) {
                    ctx->queue[j] = ctx->queue[j + 1U];
                }
                ctx->count--;
            }
        }
    }
    ctx->frozen_slots = 0U;
    ctx->frozen_count = 0U;
    if (ctx->count > 0U) {
        ctx->frozen_slots = event_memory_rebuild(ctx);
        ctx->frozen = true;
        ctx->flag = true;
    }
    else {
        (void) memset(ctx->memory, 0, sizeof(ctx->memory));
        ctx->flag = false;
    }
    IOLINK_CRITICAL_EXIT();
}

bool iolink_events_pending(const iolink_events_ctx_t* ctx)
{
    /* Single byte read is typically atomic, avoiding lock for perf */
    return ((ctx != NULL) && (ctx->count > 0U));
}

bool iolink_events_pop(iolink_events_ctx_t* ctx, iolink_event_t* event)
{
    if ((ctx == NULL) || (event == NULL)) {
        return false;
    }

    bool ret = false;
    IOLINK_CRITICAL_ENTER();

    if (ctx->count > 0U) {
        *event = ctx->queue[0];
        for (uint8_t i = 1U; i < ctx->count; i++) {
            ctx->queue[i - 1U] = ctx->queue[i];
        }
        ctx->count--;
        if (ctx->count == 0U) {
            /* Keep flag == (count > 0) even when an application drains the
               queue through pop() instead of acknowledging the event memory. */
            ctx->flag = false;
            ctx->frozen = false;
            ctx->frozen_slots = 0U;
            (void) memset(ctx->memory, 0, sizeof(ctx->memory));
        }
        ret = true;
    }

    IOLINK_CRITICAL_EXIT();
    return ret;
}

bool iolink_events_peek(const iolink_events_ctx_t* ctx, iolink_event_t* event)
{
    if ((ctx == NULL) || (event == NULL)) {
        return false;
    }

    /* Single read is atomic for small structs, but use critical section for safety */
    IOLINK_CRITICAL_ENTER();

    bool ret = false;
    if (ctx->count > 0U) {
        *event = ctx->queue[0];
        ret = true;
    }

    IOLINK_CRITICAL_EXIT();
    return ret;
}

/** @brief DeviceStatus priority per Table B.13 (lowest value dominates). */
static uint8_t device_status_priority(uint8_t status)
{
    switch (status) {
        case 4U: /* Failure */
            return 1U;
        case 3U: /* Functional-Check */
            return 2U;
        case 1U: /* Maintenance-Required */
            return 3U;
        case 2U: /* Out-of-Specification */
            return 4U;
        default: /* 0 = OK */
            return 5U;
    }
}

/** @brief Map an event to its DeviceStatus value (Table B.13), from its
 * EventType and, for the codes whose preferred value differs, its EventCode. */
static uint8_t event_type_device_status(uint16_t code, iolink_event_type_t type)
{
    switch (code) {
        case IOLINK_EVENTCODE_SIMULATION_ACTIVE:
            return 3U; /* Functional-Check (Table D.1 preferred value) */
        case IOLINK_EVENTCODE_MAINTENANCE_CLEANING:
        case IOLINK_EVENTCODE_MAINTENANCE_REFILL:
        case IOLINK_EVENTCODE_MAINTENANCE_WEAR:
            return 1U; /* Maintenance-Required (Table D.1 preferred value) */
        default:
            break;
    }
    switch (type) {
        case IOLINK_EVENT_TYPE_NOTIFICATION:
            return 0U; /* OK */
        case IOLINK_EVENT_TYPE_WARNING:
            return 2U; /* Out-of-Specification */
        default:
            return 4U; /* Failure */
    }
}

uint8_t iolink_events_get_highest_severity(iolink_events_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->count == 0U)) {
        return 0U; /* OK */
    }

    uint8_t worst = 0U;      /* OK */
    uint8_t worst_prio = 5U; /* OK priority */
    IOLINK_CRITICAL_ENTER();
    for (uint8_t i = 0U; i < ctx->count; i++) {
        const iolink_event_t* ev = &ctx->queue[i];
        const uint8_t status = event_type_device_status(ev->code, ev->type);
        const uint8_t prio = device_status_priority(status);
        if (prio < worst_prio) {
            worst_prio = prio;
            worst = status;
        }
    }
    IOLINK_CRITICAL_EXIT();
    return worst;
}

uint8_t iolink_events_get_all(iolink_events_ctx_t* ctx, iolink_event_t* out_events,
                              uint8_t max_count)
{
    if ((ctx == NULL) || (out_events == NULL) || (max_count == 0U)) {
        return 0U;
    }

    uint8_t copied = 0U;
    IOLINK_CRITICAL_ENTER();
    uint8_t count = ctx->count;
    uint8_t to_copy = (count < max_count) ? count : max_count;

    for (uint8_t i = 0U; i < to_copy; i++) {
        out_events[i] = ctx->queue[i];
        copied++;
    }
    IOLINK_CRITICAL_EXIT();
    return copied;
}

iolink_event_type_t iolink_event_classify(uint16_t code)
{
    switch (code) {
        case IOLINK_EVENTCODE_NO_MALFUNCTION:
        case IOLINK_EVENTCODE_DS_UPLOAD_REQUEST:
            return IOLINK_EVENT_TYPE_NOTIFICATION;

        case IOLINK_EVENTCODE_TEMPERATURE_OVERRUN:
        case IOLINK_EVENTCODE_TEMPERATURE_UNDERRUN:
        case IOLINK_EVENTCODE_BATTERY_LOW:
        case IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN:
        case IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN:
        case IOLINK_EVENTCODE_SIMULATION_ACTIVE:
        case IOLINK_EVENTCODE_PV_RANGE_OVERRUN:
        case IOLINK_EVENTCODE_PV_RANGE_UNDERRUN:
        case IOLINK_EVENTCODE_MAINTENANCE_CLEANING:
        case IOLINK_EVENTCODE_MAINTENANCE_REFILL:
        case IOLINK_EVENTCODE_MAINTENANCE_WEAR:
            return IOLINK_EVENT_TYPE_WARNING;

        case IOLINK_EVENTCODE_GENERAL_MALFUNCTION:
        case IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD:
        case IOLINK_EVENTCODE_HARDWARE_FAULT:
        case IOLINK_EVENTCODE_COMPONENT_MALFUNCTION:
        case IOLINK_EVENTCODE_NVM_LOSS:
        case IOLINK_EVENTCODE_POWER_SUPPLY_FAULT:
        case IOLINK_EVENTCODE_FUSE_BLOWN:
        case IOLINK_EVENTCODE_SOFTWARE_FAULT:
        case IOLINK_EVENTCODE_PARAMETER_ERROR:
        case IOLINK_EVENTCODE_PARAMETER_MISSING:
        case IOLINK_EVENTCODE_WIRE_BREAK:
        case IOLINK_EVENTCODE_SHORT_CIRCUIT:
        case IOLINK_EVENTCODE_GROUND_FAULT:
        case IOLINK_EVENTCODE_APPLICATION_FAULT:
        case IOLINK_EVENTCODE_MEASUREMENT_RANGE_EXCEEDED:
            return IOLINK_EVENT_TYPE_ERROR;

        default:
            /* Reserved / vendor-specific / unknown -> safe default. */
            return IOLINK_EVENT_TYPE_ERROR;
    }
}

#endif /* IOLINK_FEATURE_EVENTS */
