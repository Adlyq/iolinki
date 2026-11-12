/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file events.c
 * @brief IO-Link 事件处理
 * @ingroup iolinki_events
 *
 * 实现设备事件队列（入队/弹出/窥视）、严重级别排序以及
 * 用于向主站上报诊断的事件码分类。
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

/** @brief 来自已绑定应用时钟的当前时间（毫秒）。 */
static uint32_t events_now_ms(const iolink_events_ctx_t* ctx)
{
    if (ctx->time_us == NULL) {
        return 0U;
    }
    return (uint32_t) (ctx->time_us(ctx->time_user) / 1000U);
}

/** @brief 由事件类型和 MODE 生成 EventQualifier（A.6.4，Figure A.24）。 */
static uint8_t event_qualifier(iolink_event_type_t type, iolink_event_mode_t mode)
{
    uint8_t q;
    switch (mode) {
        case IOLINK_EVENT_MODE_DISAPPEARS:
            q = 0x80U; /* MODE = 事件消失（0b10） */
            break;
        case IOLINK_EVENT_MODE_SINGLE_SHOT:
            q = 0x40U; /* MODE = 事件单次触发（0b01） */
            break;
        default:
            q = 0xC0U; /* MODE = 事件出现（0b11） */
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
    q |= 0x04U; /* SOURCE = 设备（bit 3 = 0），INSTANCE = Application（Table A.17） */
    return q;
}

/** @brief 用队列中最旧的事件重建 Table 58 事件存储器。
 *
 * 最多填充六个槽位；StatusCode 为每个活动槽位携带一位。
 * 调用方需持有临界区。返回写入的槽位数量。 */
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
    /* StatusCode type 2：Event Details = 1，bits 0-5 每个活动槽位一位
       （Figure A.22/A.23：bit 0 为 Event 1，bit 1 为 Event 2，...）。 */
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

/** @brief 按严重级别顺序将事件插入队列（错误在前）。 */
static void event_queue_insert(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type,
                               iolink_event_mode_t mode)
{
    uint8_t pos = 0U;
    while ((pos < ctx->count) && (ctx->queue[pos].type >= type)) {
        pos++;
    }
    if (ctx->count >= IOLINK_EVENT_QUEUE_SIZE) {
        /* 队列中保存的是更严重的事件，或者最不严重的条目会在下面被
           覆盖。无论哪种情况都会丢失一个事件，因此让它像 pending[]
           溢出一样可观测。 */
        ctx->dropped_count++;
        if (pos >= IOLINK_EVENT_QUEUE_SIZE) {
            return; /* 队列保存的是更严重的事件：丢弃新的这个。 */
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

/** @brief 分配生命周期槽位，优先复用已结束重复冷却的槽位。 */
static iolink_event_pending_t* pending_alloc(iolink_events_ctx_t* ctx, uint32_t now)
{
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (!ctx->pending[i].active) {
            (void) memset(&ctx->pending[i], 0, sizeof(ctx->pending[i]));
            ctx->pending[i].active = true;
            return &ctx->pending[i];
        }
    }
    /* 仅在两个 1 s 窗口都结束后才复用已冷却的槽位。提前复用会丢弃
       最近的出现/消失时间戳，使同一个 EventCode 在 10.10.2 窗口内
       被上报两次。 */
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_pending_t* p = &ctx->pending[i];
        const bool appears_free = !p->appears_valid || ((uint32_t) (now - p->last_appears_ms) >=
                                                        IOLINK_EVENT_MIN_REPEAT_MS);
        const bool disappears_free =
            !p->disappears_valid ||
            ((uint32_t) (now - p->last_disappears_ms) >= IOLINK_EVENT_MIN_REPEAT_MS);
        if (!p->present && !p->reported && appears_free && disappears_free) {
            (void) memset(p, 0, sizeof(*p));
            p->active = true;
            return p;
        }
    }
    return NULL;
}

/** @brief 1 s 间隔是否允许新的“事件出现”。 */
static bool event_appears_elapsed(const iolink_event_pending_t* p, uint32_t now)
{
    return !p->appears_valid ||
           ((uint32_t) (now - p->last_appears_ms) >= IOLINK_EVENT_MIN_REPEAT_MS);
}

/** @brief 1 s 间隔是否允许新的“事件消失”。 */
static bool event_disappears_elapsed(const iolink_event_pending_t* p, uint32_t now)
{
    return !p->disappears_valid ||
           ((uint32_t) (now - p->last_disappears_ms) >= IOLINK_EVENT_MIN_REPEAT_MS);
}

/** @brief 将“事件出现”入队并启动最短出现窗口。 */
static void pending_emit_appears(iolink_events_ctx_t* ctx, iolink_event_pending_t* p, uint32_t now)
{
    event_queue_insert(ctx, p->code, p->type, IOLINK_EVENT_MODE_APPEARS);
    p->last_appears_ms = now;
    p->appears_valid = true;
    p->reported = true;
}

/** @brief 为已上报的事件将“事件消失”入队。 */
static void pending_emit_disappears(iolink_events_ctx_t* ctx, iolink_event_pending_t* p,
                                    uint32_t now)
{
    event_queue_insert(ctx, p->code, p->type, IOLINK_EVENT_MODE_DISAPPEARS);
    p->last_disappears_ms = now;
    p->disappears_valid = true;
    p->reported = false;
}

/** @brief 冻结存储器并置位标志（Table 60 T3）。
 *
 * 队列为空时绝不能置位该标志：置位的标志会告诉主站
 * 读取诊断通道，而那里若队列为空，就会产生
 * 读取/确认循环。 */
static void event_publish(iolink_events_ctx_t* ctx)
{
    if (!ctx->frozen && (ctx->count > 0U)) {
        ctx->frozen_slots = event_memory_rebuild(ctx);
        ctx->frozen = true;
    }
    ctx->flag = (ctx->count > 0U);
}

/** @brief @p code 是否已入队（任意 MODE）。 */
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

    IOLINK_LOG_DEBUG(&ctx->log, "event trigger code=0x%04X type=%u", (unsigned) code,
                     (unsigned) type);

    IOLINK_CRITICAL_ENTER();

    const uint32_t now = events_now_ms(ctx);

    if (type == IOLINK_EVENT_TYPE_NOTIFICATION) {
        /* 通知是单次触发的。在前一个实例仍未确认时，不要再次
           将同一 code 入队：重试中的主站不得把重复的请求变成
           事件风暴。 */
        if (!event_code_queued(ctx, code)) {
            event_queue_insert(ctx, code, type, IOLINK_EVENT_MODE_SINGLE_SHOT);
        }
        event_publish(ctx);
        IOLINK_CRITICAL_EXIT();
        return;
    }

    iolink_event_pending_t* p = pending_find(ctx, code);
    if ((p != NULL) && p->present) {
        /* 已置位：10.10.2 忽略同一根因的后续事件，因此不会将
           第二个“事件出现”入队。 */
        IOLINK_CRITICAL_EXIT();
        return;
    }

    if (p == NULL) {
        p = pending_alloc(ctx, now);
        if (p == NULL) {
            /* 所有生命周期槽位都保存着活动事件：让丢失可观测，
               而不是静默丢弃该原因。 */
            ctx->dropped_count++;
            IOLINK_CRITICAL_EXIT();
            return;
        }
        p->code = code;
    }

    p->present = true;
    p->type = type;
    ctx->pending_count++;

    if (!p->reported && event_appears_elapsed(p, now)) {
        /* 首次出现，或该 code 上一次“事件出现”之后超过 1 s 的
           新事件（10.10.2）。 */
        pending_emit_appears(ctx, p, now);
    }
    /* 否则该出现要么已经在线上（此前有待处理的撤销，现已取消），
       要么因为相同的 EventQualifier 在不到 1 s 前被上报过而被推迟到
       iolink_events_process() 处理。 */

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
    if ((p != NULL) && p->present) {
        p->present = false; /* 由 iolink_events_process() 发出 */
        if (ctx->pending_count > 0U) {
            ctx->pending_count--;
        }
        IOLINK_CRITICAL_EXIT();
        IOLINK_LOG_DEBUG(&ctx->log, "event clear code=0x%04X", (unsigned) code);
        return;
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
    bool queued = false;
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_pending_t* p = &ctx->pending[i];
        if (!p->active) {
            continue;
        }
        if (p->present && !p->reported) {
            /* 推迟的出现：1 s 间隔已过。 */
            if (event_appears_elapsed(p, now)) {
                pending_emit_appears(ctx, p, now);
                queued = true;
            }
            continue;
        }
        if (!p->present && p->reported) {
            /* A.6.4：“出现”后不到 50 ms 不得有“消失”；10.10.2：
               1 s 窗口内不得重复“消失”。 */
            if (((uint32_t) (now - p->last_appears_ms) >= IOLINK_EVENT_MIN_APPEAR_MS) &&
                event_disappears_elapsed(p, now)) {
                pending_emit_disappears(ctx, p, now);
                queued = true;
            }
        }
        if (!p->present && !p->reported && event_appears_elapsed(p, now) &&
            event_disappears_elapsed(p, now)) {
            /* 事件已消失且其 1 s 冷却已过：释放槽位。 */
            (void) memset(p, 0, sizeof(*p));
        }
    }
    if (queued) {
        event_publish(ctx);
    }
    IOLINK_CRITICAL_EXIT();
}

void iolink_events_clear_diagnosis(iolink_events_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    IOLINK_CRITICAL_ENTER();

    /* Table 101 “Clear”：队列中的通知不携带诊断信息，在仍未确认时
       会被丢弃。 */
    uint8_t kept = 0U;
    for (uint8_t i = 0U; i < ctx->count; i++) {
        if (ctx->queue[i].type == IOLINK_EVENT_TYPE_NOTIFICATION) {
            continue;
        }
        ctx->queue[kept] = ctx->queue[i];
        kept++;
    }
    ctx->count = kept;

    /* 撤销所有已置位的错误/警告。复位是刻意的，因此“事件消失”会
       立即入队，而不等待常规的 A.6.4/10.10.2 间隔；DeviceStatus 随
       复位一并清除。 */
    const uint32_t now = events_now_ms(ctx);
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_pending_t* p = &ctx->pending[i];
        if (!p->active || !p->present) {
            continue;
        }
        p->present = false;
        if (ctx->pending_count > 0U) {
            ctx->pending_count--;
        }
        if (p->reported) {
            pending_emit_disappears(ctx, p, now);
        }
    }

    if (ctx->count == 0U) {
        ctx->frozen = false;
        ctx->frozen_count = 0U;
        (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    }
    event_publish(ctx);
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
    ctx->dropped_count = 0U;
    (void) memset(ctx->pending, 0, sizeof(ctx->pending));
    (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    ctx->frozen = false;
    ctx->flag = false;
    ctx->frozen_slots = 0U;
    ctx->frozen_count = 0U;
    (void) memset(ctx->frozen_events, 0, sizeof(ctx->frozen_events));
    IOLINK_CRITICAL_EXIT();
}

bool iolink_events_flag(const iolink_events_ctx_t* ctx)
{
    /* 绝不在队列为空时通告事件标志。各设置函数保持
       flag == (count > 0)；此保护也覆盖并发的 ISR 读取。 */
    return ((ctx != NULL) && ctx->flag && (ctx->count > 0U));
}

uint32_t iolink_events_get_dropped_count(const iolink_events_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->dropped_count : 0U;
}

uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr)
{
    if ((ctx == NULL) || (addr >= sizeof(ctx->memory))) {
        return 0U;
    }
    IOLINK_CRITICAL_ENTER();
    /* 事件标志置位时存储器已被冻结（Table 60 T3），因此读出的
       始终是一致快照。 */
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
    /* Table 60 T5：确认会释放存储器并清除标志。仅确认实际呈现过的
       槽位（frozen_slots）；冻结期间入队的事件会在下面重建并再次
       可见。 */
    ctx->frozen = false;
    ctx->flag = false;
    if (ctx->frozen_slots > 0U) {
        /* 只确认冻结存储器中实际呈现的事件；冻结之后入队的更高
           严重级别事件必须保留。 */
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
    /* 单字节读取通常是原子的，为性能避免加锁 */
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
            /* 即使应用通过 pop() 排空队列而不是确认事件存储器，
               也保持 flag == (count > 0)。 */
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

    /* 对小型结构体单次读取是原子的，但为安全仍使用临界区 */
    IOLINK_CRITICAL_ENTER();

    bool ret = false;
    if (ctx->count > 0U) {
        *event = ctx->queue[0];
        ret = true;
    }

    IOLINK_CRITICAL_EXIT();
    return ret;
}

/** @brief 按 Table B.13 的 DeviceStatus 优先级（数值最小者优先）。 */
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

/** @brief 根据 EventType 将事件映射为其 DeviceStatus 值（Table B.13）；
 * 对于首选值不同的 code，则根据其 EventCode 映射。 */
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
    if (ctx == NULL) {
        return 0U; /* OK */
    }

    /* Table B.13 由当前活动的条件推导，而不是由队列推导：
       队列中的“事件消失”不得让 DeviceStatus 保持置位，而通知
       绝不会提升它。 */
    uint8_t worst = 0U;      /* OK */
    uint8_t worst_prio = 5U; /* OK 优先级 */
    IOLINK_CRITICAL_ENTER();
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        const iolink_event_pending_t* p = &ctx->pending[i];
        if (!p->active || !p->present || !p->reported ||
            (p->type == IOLINK_EVENT_TYPE_NOTIFICATION)) {
            continue;
        }
        const uint8_t status = event_type_device_status(p->code, p->type);
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
            /* 保留 / 厂商特定 / 未知 -> 安全默认值。 */
            return IOLINK_EVENT_TYPE_ERROR;
    }
}

#endif /* IOLINK_FEATURE_EVENTS */
