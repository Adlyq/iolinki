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

/** @brief EventQualifier for an event type (A.6.4, Figure A.24). */
static uint8_t event_qualifier(iolink_event_type_t type)
{
    uint8_t q = 0xC0U; /* MODE = event appears */
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
        uint8_t idx = (uint8_t) ((ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        const iolink_event_t* ev = &ctx->queue[idx];
        ctx->memory[1U + i * 3U] = event_qualifier(ev->type);
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
    return slots;
}

void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type)
{
    if (ctx == NULL) {
        return;
    }

    iolink_critical_enter();

    if (ctx->count >= IOLINK_EVENT_QUEUE_SIZE) {
        /* Drop oldest (inline to avoid recursive lock) */
        ctx->head = (uint8_t) ((ctx->head + 1U) % IOLINK_EVENT_QUEUE_SIZE);
        ctx->count--;
    }

    ctx->queue[ctx->tail].code = code;
    ctx->queue[ctx->tail].type = type;
    ctx->tail = (uint8_t) ((ctx->tail + 1U) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count++;

    /* Table 60 T3: freeze the memory as soon as the Event flag is raised.
       Events queued afterwards stay invisible until the StatusCode
       confirmation releases the memory (T5). */
    if (!ctx->frozen) {
        ctx->frozen_slots = event_memory_rebuild(ctx);
        ctx->frozen = true;
    }
    ctx->flag = true;

    iolink_critical_exit();
}

bool iolink_events_flag(const iolink_events_ctx_t* ctx)
{
    return ((ctx != NULL) && ctx->flag);
}

uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr)
{
    if ((ctx == NULL) || (addr >= sizeof(ctx->memory))) {
        return 0U;
    }
    iolink_critical_enter();
    /* The memory was frozen when the Event flag was raised (Table 60 T3), so a
       readout is always a consistent snapshot. */
    const uint8_t value = ctx->memory[addr];
    iolink_critical_exit();
    return value;
}

void iolink_events_memory_write(iolink_events_ctx_t* ctx, uint8_t addr, uint8_t value)
{
    (void) value;
    if ((ctx == NULL) || (addr != 0U)) {
        return;
    }

    iolink_critical_enter();
    /* Table 60 T5: confirmation releases the memory and clears the flag. Only
       the slots actually presented (frozen_slots) are acknowledged; events
       queued while frozen are rebuilt below and become visible again. */
    ctx->frozen = false;
    ctx->flag = false;
    uint8_t acked = ctx->frozen_slots;
    if (acked == 0U) {
        /* No frozen snapshot was taken (the master confirmed without reading):
           acknowledge whatever is currently presented (bounded by the 6 slots). */
        acked = ctx->count;
        if (acked > 6U) {
            acked = 6U;
        }
    }
    if (acked > ctx->count) {
        acked = ctx->count;
    }
    ctx->head = (uint8_t) ((ctx->head + acked) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count = (uint8_t) (ctx->count - acked);
    ctx->frozen_slots = 0U;
    if (ctx->count > 0U) {
        ctx->frozen_slots = event_memory_rebuild(ctx);
        ctx->frozen = true;
        ctx->flag = true;
    }
    else {
        (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    }
    iolink_critical_exit();
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
    iolink_critical_enter();

    if (ctx->count > 0U) {
        *event = ctx->queue[ctx->head];
        ctx->head = (uint8_t) ((ctx->head + 1U) % IOLINK_EVENT_QUEUE_SIZE);
        ctx->count--;
        ret = true;
    }

    iolink_critical_exit();
    return ret;
}

bool iolink_events_peek(const iolink_events_ctx_t* ctx, iolink_event_t* event)
{
    if ((ctx == NULL) || (event == NULL)) {
        return false;
    }

    /* Single read is atomic for small structs, but use critical section for safety */
    iolink_critical_enter();

    bool ret = false;
    if (ctx->count > 0U) {
        *event = ctx->queue[ctx->head];
        ret = true;
    }

    iolink_critical_exit();
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
    iolink_critical_enter();
    for (uint8_t i = 0U; i < ctx->count; i++) {
        uint8_t idx = (uint8_t) ((ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        const iolink_event_t* ev = &ctx->queue[idx];
        const uint8_t status = event_type_device_status(ev->code, ev->type);
        const uint8_t prio = device_status_priority(status);
        if (prio < worst_prio) {
            worst_prio = prio;
            worst = status;
        }
    }
    iolink_critical_exit();
    return worst;
}

uint8_t iolink_events_get_all(iolink_events_ctx_t* ctx, iolink_event_t* out_events,
                              uint8_t max_count)
{
    if ((ctx == NULL) || (out_events == NULL) || (max_count == 0U)) {
        return 0U;
    }

    uint8_t copied = 0U;
    iolink_critical_enter();
    uint8_t count = ctx->count;
    uint8_t to_copy = (count < max_count) ? count : max_count;

    for (uint8_t i = 0U; i < to_copy; i++) {
        uint8_t idx = (uint8_t) ((ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        out_events[i] = ctx->queue[idx];
        copied++;
    }
    iolink_critical_exit();
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
