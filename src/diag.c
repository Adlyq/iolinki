/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file diag.c
 * @brief 诊断源注册表与派生视图。
 * @ingroup iolinki_diag
 *
 * 本文件是 EventCode → EventType / DeviceStatus / EventQualifier 的唯一映射点，
 * 也是 DeviceStatus（0x0024）与 DetailedDeviceStatus（0x0025）的唯一派生点。
 * 运行时状态（条件锁存、appears/disappears 生命周期）仍由事件引擎持有；
 * 这里只保存来源自身的采样事实。
 */

#include "iolinki/diag.h"

#include "iolinki/events.h"
#include "iolinki/port.h"
#include "iolinki/utils.h"

#if IOLINK_FEATURE_EVENTS

/* 来源条目必须与 iolink_diag_src_t 的顺序一致，前 8 项即来源表。 */
static const iolink_diag_desc_t diag_registry[] = {
    /* code     hold_ms  type                      status  flags  src */
    {0x5111U, 0U, IOLINK_EVENT_TYPE_WARNING, 2U, IOLINK_DIAG_F_LEVEL | IOLINK_DIAG_F_LATCHING,
     (uint8_t) IOLINK_DIAG_SRC_PHY_UNDERVOLTAGE},
    {0x5110U, 0U, IOLINK_EVENT_TYPE_WARNING, 2U, IOLINK_DIAG_F_LEVEL | IOLINK_DIAG_F_LATCHING,
     (uint8_t) IOLINK_DIAG_SRC_PHY_OVERVOLTAGE},
    {0x4000U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, IOLINK_DIAG_F_LEVEL | IOLINK_DIAG_F_LATCHING,
     (uint8_t) IOLINK_DIAG_SRC_PHY_OVERTEMP},
    {0x7710U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, IOLINK_DIAG_F_LEVEL | IOLINK_DIAG_F_LATCHING,
     (uint8_t) IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT},
    {0x1801U, IOLINK_DIAG_PULSE_HOLD_MS, IOLINK_EVENT_TYPE_WARNING, 2U, IOLINK_DIAG_F_PULSE,
     (uint8_t) IOLINK_DIAG_SRC_COMM_CRC},
    {0x1802U, IOLINK_DIAG_PULSE_HOLD_MS, IOLINK_EVENT_TYPE_WARNING, 2U, IOLINK_DIAG_F_PULSE,
     (uint8_t) IOLINK_DIAG_SRC_COMM_TIMEOUT},
    {0x1803U, IOLINK_DIAG_PULSE_HOLD_MS, IOLINK_EVENT_TYPE_ERROR, 4U, IOLINK_DIAG_F_PULSE,
     (uint8_t) IOLINK_DIAG_SRC_COMM_FRAMING},
    {0x1804U, IOLINK_DIAG_PULSE_HOLD_MS, IOLINK_EVENT_TYPE_WARNING, 2U, IOLINK_DIAG_F_PULSE,
     (uint8_t) IOLINK_DIAG_SRC_COMM_TIMING},

    /* Table D.1 标准设备代码（Type / Preferred DeviceStatus 逐条来自规范）。 */
    {0x0000U, 0U, IOLINK_EVENT_TYPE_NOTIFICATION, 0U, IOLINK_DIAG_F_NOTIFICATION,
     IOLINK_DIAG_SRC_NONE},
    {0x1000U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x4210U, 0U, IOLINK_EVENT_TYPE_WARNING, 2U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x4220U, 0U, IOLINK_EVENT_TYPE_WARNING, 2U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x5000U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x5010U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x5011U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x5012U, 0U, IOLINK_EVENT_TYPE_WARNING, 2U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x5100U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x5101U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x6000U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x6320U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x6321U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x7700U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x7711U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C00U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C01U, 0U, IOLINK_EVENT_TYPE_WARNING, 3U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C10U, 0U, IOLINK_EVENT_TYPE_WARNING, 2U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C20U, 0U, IOLINK_EVENT_TYPE_ERROR, 4U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C30U, 0U, IOLINK_EVENT_TYPE_WARNING, 2U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C40U, 0U, IOLINK_EVENT_TYPE_WARNING, 1U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C41U, 0U, IOLINK_EVENT_TYPE_WARNING, 1U, 0U, IOLINK_DIAG_SRC_NONE},
    {0x8C42U, 0U, IOLINK_EVENT_TYPE_WARNING, 1U, 0U, IOLINK_DIAG_SRC_NONE},
    {0xFF91U, 0U, IOLINK_EVENT_TYPE_NOTIFICATION, 0U, IOLINK_DIAG_F_NOTIFICATION,
     IOLINK_DIAG_SRC_NONE},
};

#define DIAG_REGISTRY_SIZE ((uint8_t) (sizeof(diag_registry) / sizeof(diag_registry[0])))

/** @brief 条件表按来源索引：来源条目固定排在注册表前 IOLINK_DIAG_SRC_COUNT 项。 */
static const iolink_diag_desc_t* diag_desc_by_src(iolink_diag_src_t src)
{
    if ((uint8_t) src >= (uint8_t) IOLINK_DIAG_SRC_COUNT) {
        return NULL;
    }
    const iolink_diag_desc_t* desc = &diag_registry[(uint8_t) src];
    return (desc->src == (uint8_t) src) ? desc : NULL;
}

/** @brief 已绑定事件的当前时间（毫秒）。 */
static uint32_t diag_now_ms(const iolink_events_ctx_t* events)
{
    if ((events == NULL) || (events->time_us == NULL)) {
        return 0U;
    }
    return (uint32_t) (events->time_us(events->time_user) / 1000U);
}

/** @brief 按 EventCode 查找已分配的条件槽位（任意状态）。 */
static iolink_event_pending_t* diag_cond_find(iolink_events_ctx_t* events, uint16_t code)
{
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (events->pending[i].active && (events->pending[i].code == code)) {
            return &events->pending[i];
        }
    }
    return NULL;
}

/** @brief 条件的唯一触发入口：生命周期由事件引擎维护，这里只补齐来源标记。 */
static void diag_raise(iolink_events_ctx_t* events, iolink_diag_src_t src, uint16_t code,
                       uint8_t type)
{
    iolink_event_trigger(events, code, (iolink_event_type_t) type);
    /* 触发可能因槽位满而被丢弃；来源标记只在该 code 仍有槽位时写入。 */
    IOLINK_CRITICAL_ENTER();
    iolink_event_pending_t* p = diag_cond_find(events, code);
    if (p != NULL) {
        p->source = (uint8_t) src;
    }
    IOLINK_CRITICAL_EXIT();
}

/** @brief 在给定时间戳上处理一次脉冲（主上下文，供 service() 复用）。 */
static void diag_pulse_at(iolink_events_ctx_t* events, iolink_diag_src_t src, uint32_t now)
{
    const iolink_diag_desc_t* desc = diag_desc_by_src(src);
    if (desc == NULL) {
        return;
    }
    events->diag.quiet_since_ms[(uint8_t) src] = now;
    events->diag.edges[(uint8_t) src]++;
    diag_raise(events, src, desc->code, desc->type);
}

uint8_t iolink_diag_registry_size(void)
{
    return DIAG_REGISTRY_SIZE;
}

const iolink_diag_desc_t* iolink_diag_registry_at(uint8_t index)
{
    return (index < DIAG_REGISTRY_SIZE) ? &diag_registry[index] : NULL;
}

const iolink_diag_desc_t* iolink_diag_desc(uint16_t code)
{
    for (uint8_t i = 0U; i < DIAG_REGISTRY_SIZE; i++) {
        if (diag_registry[i].code == code) {
            return &diag_registry[i];
        }
    }
    return NULL;
}

const iolink_diag_desc_t* iolink_diag_src_desc(iolink_diag_src_t src)
{
    return diag_desc_by_src(src);
}

uint16_t iolink_diag_src_code(iolink_diag_src_t src)
{
    const iolink_diag_desc_t* desc = diag_desc_by_src(src);
    return (desc != NULL) ? desc->code : 0U;
}

uint8_t iolink_diag_type_for(uint16_t code, uint8_t fallback)
{
    const iolink_diag_desc_t* desc = iolink_diag_desc(code);
    return (desc != NULL) ? desc->type : fallback;
}

uint8_t iolink_diag_status_for(uint16_t code, uint8_t type)
{
    const iolink_diag_desc_t* desc = iolink_diag_desc(code);
    if ((desc != NULL) && (desc->status != IOLINK_DIAG_STATUS_NONE)) {
        return desc->status;
    }
    switch (type) {
        case (uint8_t) IOLINK_EVENT_TYPE_NOTIFICATION:
            return 0U;
        case (uint8_t) IOLINK_EVENT_TYPE_WARNING:
            return 2U;
        default:
            return 4U;
    }
}

void iolink_diag_init(iolink_events_ctx_t* events)
{
    iolink_diag_reset(events);
}

void iolink_diag_reset(iolink_events_ctx_t* events)
{
    if (events == NULL) {
        return;
    }
    (void) iolink_ctx_zero(&events->diag, sizeof(events->diag));
}

bool iolink_diag_set_level(iolink_events_ctx_t* events, iolink_diag_src_t src, bool level)
{
    const iolink_diag_desc_t* desc = diag_desc_by_src(src);
    if ((events == NULL) || (desc == NULL) || ((desc->flags & IOLINK_DIAG_F_LEVEL) == 0U)) {
        return false;
    }

    const iolink_event_pending_t* p = diag_cond_find(events, desc->code);
    const bool present = (p != NULL) && p->present;

    if (level) {
        if (present) {
            return false; /* 稳态：同一条件不再重复上报（10.10.2）。 */
        }
        events->diag.edges[(uint8_t) src]++;
        diag_raise(events, src, desc->code, desc->type);
        return true;
    }
    if (present) {
        /* 物理恢复：present 立即清；disappears 由事件引擎按窗口发出。 */
        iolink_event_clear(events, desc->code);
    }
    return false;
}

void iolink_diag_pulse(iolink_events_ctx_t* events, iolink_diag_src_t src)
{
    const iolink_diag_desc_t* desc = diag_desc_by_src(src);
    if ((events == NULL) || (desc == NULL) || ((desc->flags & IOLINK_DIAG_F_PULSE) == 0U)) {
        return;
    }
    diag_pulse_at(events, src, diag_now_ms(events));
}

void iolink_diag_pulse_isr(iolink_events_ctx_t* events, iolink_diag_src_t src)
{
    if ((events == NULL) || ((uint8_t) src >= (uint8_t) IOLINK_DIAG_SRC_COUNT)) {
        return;
    }
    /* 单写者单字节存储：不与主上下文的条件表发生读-改-写竞争。 */
    events->diag.isr_pulse[(uint8_t) src] = 1U;
}

void iolink_diag_service(iolink_events_ctx_t* events, uint32_t now_ms)
{
    if (events == NULL) {
        return;
    }

    /* 先消费 ISR one-shot。空扫描只读 volatile 字节，不取临界区；
     * 只有确实有待处理脉冲时才进入一次临界区完成读-清。 */
    uint8_t fired_mask = 0U;
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_DIAG_SRC_COUNT; i++) {
        if (events->diag.isr_pulse[i] != 0U) {
            fired_mask = (uint8_t) (fired_mask | (uint8_t) (1U << i));
        }
    }
    if (fired_mask != 0U) {
        IOLINK_CRITICAL_ENTER();
        for (uint8_t i = 0U; i < (uint8_t) IOLINK_DIAG_SRC_COUNT; i++) {
            if (events->diag.isr_pulse[i] != 0U) {
                events->diag.isr_pulse[i] = 0U;
                fired_mask = (uint8_t) (fired_mask | (uint8_t) (1U << i));
            }
        }
        IOLINK_CRITICAL_EXIT();
    }
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_DIAG_SRC_COUNT; i++) {
        if ((fired_mask & (uint8_t) (1U << i)) != 0U) {
            const iolink_diag_desc_t* desc = diag_desc_by_src((iolink_diag_src_t) i);
            /* 与 iolink_diag_pulse() 一致：只有 PULSE 源接受 one-shot。 */
            if ((desc != NULL) && ((desc->flags & IOLINK_DIAG_F_PULSE) != 0U)) {
                diag_pulse_at(events, (iolink_diag_src_t) i, now_ms);
            }
        }
    }

    /* 再关闭 PULSE 保持窗口：最后一次发生后的 hold_ms 内不撤销条件。 */
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_DIAG_SRC_COUNT; i++) {
        const iolink_diag_desc_t* desc = diag_desc_by_src((iolink_diag_src_t) i);
        if ((desc == NULL) || ((desc->flags & IOLINK_DIAG_F_PULSE) == 0U) ||
            (desc->hold_ms == 0U)) {
            continue;
        }
        const iolink_event_pending_t* p = diag_cond_find(events, desc->code);
        if ((p == NULL) || !p->present) {
            continue;
        }
        const uint32_t quiet_ms = (uint32_t) (now_ms - events->diag.quiet_since_ms[i]);
        if (quiet_ms >= (uint32_t) desc->hold_ms) {
            iolink_event_clear(events, desc->code);
        }
    }
}

void iolink_diag_rearm(iolink_events_ctx_t* events, uint8_t src_mask)
{
    if (events == NULL) {
        return;
    }
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_DIAG_SRC_COUNT; i++) {
        const iolink_diag_desc_t* desc = diag_desc_by_src((iolink_diag_src_t) i);
        if ((desc == NULL) || ((src_mask & (uint8_t) (1U << i)) == 0U) ||
            ((desc->flags & IOLINK_DIAG_F_LEVEL) == 0U)) {
            continue;
        }
        const iolink_event_pending_t* p = diag_cond_find(events, desc->code);
        if ((p != NULL) && p->present) {
            /* 只清 present；reported 保留，避免重复 appears/事件风暴。 */
            iolink_event_clear(events, desc->code);
        }
    }
}

/** @brief Table B.13 优先级：数值越小越严重。 */
static uint8_t diag_status_priority(uint8_t status)
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

uint8_t iolink_diag_device_status(iolink_events_ctx_t* events)
{
    if (events == NULL) {
        return 0U;
    }

    uint8_t worst = 0U;
    uint8_t worst_prio = 5U;
    IOLINK_CRITICAL_ENTER();
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        const iolink_event_pending_t* p = &events->pending[i];
        /* present || reported：disappears 仍在队列/DDS 中时状态不回 0。 */
        if (!p->active || (!p->present && !p->reported) ||
            (p->type == IOLINK_EVENT_TYPE_NOTIFICATION)) {
            continue;
        }
        const uint8_t status = iolink_diag_status_for(p->code, (uint8_t) p->type);
        const uint8_t prio = diag_status_priority(status);
        if (prio < worst_prio) {
            worst_prio = prio;
            worst = status;
        }
    }
    IOLINK_CRITICAL_EXIT();
    return worst;
}

uint8_t iolink_diag_dds_snapshot(iolink_events_ctx_t* events, iolink_diag_dds_entry_t* out,
                                 uint8_t max)
{
    if ((events == NULL) || (out == NULL) || (max == 0U)) {
        return 0U;
    }

    uint8_t n = 0U;
    IOLINK_CRITICAL_ENTER();
    for (uint8_t i = 0U; (i < IOLINK_EVENT_PENDING_MAX) && (n < max); i++) {
        const iolink_event_pending_t* p = &events->pending[i];
        if (!p->active || !p->reported || (p->type == IOLINK_EVENT_TYPE_NOTIFICATION)) {
            continue;
        }
        out[n].code = p->code;
        out[n].qualifier = iolink_event_qualifier(p->code, p->type, IOLINK_EVENT_MODE_APPEARS);
        n++;
    }
    IOLINK_CRITICAL_EXIT();
    return n;
}

void iolink_diag_raise_code(iolink_events_ctx_t* events, uint16_t code)
{
    if (events == NULL) {
        return;
    }
    iolink_event_trigger(events, code,
                         (iolink_event_type_t) iolink_diag_type_for(code, IOLINK_EVENT_TYPE_ERROR));
}

void iolink_diag_clear_code(iolink_events_ctx_t* events, uint16_t code)
{
    if (events == NULL) {
        return;
    }
    iolink_event_clear(events, code);
}

uint32_t iolink_diag_edge_count(const iolink_events_ctx_t* events, iolink_diag_src_t src)
{
    if ((events == NULL) || ((uint8_t) src >= (uint8_t) IOLINK_DIAG_SRC_COUNT)) {
        return 0U;
    }
    return events->diag.edges[(uint8_t) src];
}

#endif /* IOLINK_FEATURE_EVENTS */
