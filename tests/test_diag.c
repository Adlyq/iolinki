/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_diag.c
 * @brief 诊断注册表（P10）：注册表内容、来源语义、视图派生与线上一致性。
 *
 * 期望值独立写在本文件里（不复用实现表），这样“实现与测试共用同一张表”
 * 的自证陷阱不会掩盖 Table D.1 的映射错误。时间全部由固定计数器驱动。
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include "iolinki/diag.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/events.h"
#include "iolinki/phy.h"

static uint32_t g_ms;

static uint64_t diag_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_ms * 1000U;
}

static void diag_events_init(iolink_events_ctx_t* events)
{
    g_ms = 0U;
    iolink_events_init(events);
    events->time_us = diag_time_us;
}

/** @brief 独立于实现的 Table D.1 期望表。 */
static const struct
{
    uint16_t code;
    uint8_t type;
    uint8_t status;
} k_table_d1[] = {
    {0x0000U, IOLINK_EVENT_TYPE_NOTIFICATION, 0U},
    {0x1000U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x4000U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x4210U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x4220U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x5000U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x5010U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x5011U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x5012U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x5100U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x5101U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x5110U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x5111U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x6000U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x6320U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x6321U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x7700U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x7710U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x7711U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x8C00U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x8C01U, IOLINK_EVENT_TYPE_WARNING, 3U},
    {0x8C10U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x8C20U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x8C30U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x8C40U, IOLINK_EVENT_TYPE_WARNING, 1U},
    {0x8C41U, IOLINK_EVENT_TYPE_WARNING, 1U},
    {0x8C42U, IOLINK_EVENT_TYPE_WARNING, 1U},
    {0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION, 0U},
    /* 厂商特定设备事件：类型/首选值由本栈注册表固定。 */
    {0x1801U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x1802U, IOLINK_EVENT_TYPE_WARNING, 2U},
    {0x1803U, IOLINK_EVENT_TYPE_ERROR, 4U},
    {0x1804U, IOLINK_EVENT_TYPE_WARNING, 2U},
};

static void test_registry_matches_table_d1(void** state)
{
    (void) state;

    assert_int_equal(iolink_diag_registry_size(),
                     (uint8_t) (sizeof(k_table_d1) / sizeof(k_table_d1[0])));

    for (size_t i = 0U; i < (sizeof(k_table_d1) / sizeof(k_table_d1[0])); i++) {
        const iolink_diag_desc_t* desc = iolink_diag_desc(k_table_d1[i].code);
        assert_non_null(desc);
        assert_int_equal(desc->code, k_table_d1[i].code);
        assert_int_equal(desc->type, k_table_d1[i].type);
        assert_int_equal(desc->status, k_table_d1[i].status);
        assert_int_equal(iolink_event_classify(k_table_d1[i].code), k_table_d1[i].type);
        assert_int_equal(iolink_diag_type_for(k_table_d1[i].code, 0xFFU), k_table_d1[i].type);
        assert_int_equal(iolink_diag_status_for(k_table_d1[i].code, k_table_d1[i].type),
                         k_table_d1[i].status);
    }

    /* 未登记代码：分类保持调用方输入，状态按类型回落。 */
    assert_null(iolink_diag_desc(0x1234U));
    assert_int_equal(iolink_diag_type_for(0x1234U, IOLINK_EVENT_TYPE_WARNING),
                     IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_diag_status_for(0x1234U, IOLINK_EVENT_TYPE_WARNING), 2U);
    assert_int_equal(iolink_diag_status_for(0x1234U, IOLINK_EVENT_TYPE_ERROR), 4U);
    assert_int_equal(iolink_diag_status_for(0x1234U, IOLINK_EVENT_TYPE_NOTIFICATION), 0U);

    /* 来源条目：编号 → code / 类型位。 */
    for (uint8_t src = 0U; src < (uint8_t) IOLINK_DIAG_SRC_COUNT; src++) {
        const iolink_diag_desc_t* desc = iolink_diag_src_desc((iolink_diag_src_t) src);
        assert_non_null(desc);
        assert_int_equal(desc->src, src);
        assert_int_equal(iolink_diag_src_code((iolink_diag_src_t) src), desc->code);
    }
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_PHY_UNDERVOLTAGE), 0x5111U);
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_PHY_OVERVOLTAGE), 0x5110U);
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_PHY_OVERTEMP), 0x4000U);
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT), 0x7710U);
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_COMM_CRC), 0x1801U);
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_COMM_TIMEOUT), 0x1802U);
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_COMM_FRAMING), 0x1803U);
    assert_int_equal(iolink_diag_src_code(IOLINK_DIAG_SRC_COMM_TIMING), 0x1804U);

    /* 注册表在边界外不可读。 */
    assert_null(iolink_diag_registry_at(iolink_diag_registry_size()));
    assert_null(iolink_diag_src_desc((iolink_diag_src_t) IOLINK_DIAG_SRC_COUNT));
}

/* 电平源：0→1 触发一次并计数；稳态不重复；1→0 只清条件（disappears 由
 * 事件引擎按 50 ms 窗口发出）；重新上升再计数。 */
static void test_level_source_edges(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    assert_false(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP, false));
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP), 0U);

    assert_true(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP, true));
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP), 1U);
    assert_int_equal(events.pending_count, 1U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, 0x4000U);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    /* 稳态：不重复边沿、不重复计数、不重复事件。 */
    assert_false(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP, true));
    assert_false(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP, true));
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP), 1U);
    assert_int_equal(events.count, 0U);

    /* 下降沿：present 立即清（DeviceStatus 由 present||reported 保持）。 */
    assert_false(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP, false));
    assert_int_equal(events.pending_count, 0U);
    assert_int_equal(iolink_diag_device_status(&events), 4U);

    g_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&events);
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_int_equal(iolink_diag_device_status(&events), 0U);

    /* 重新上升：新的一次边沿与出现（1 s 重复窗口已过）。 */
    g_ms = 2U * IOLINK_EVENT_MIN_REPEAT_MS;
    assert_true(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP, true));
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP), 2U);
    assert_int_equal(iolink_diag_device_status(&events), 4U);
}

/* PULSE 源：每次发生刷新保持窗口；连续脉冲只 appears 一次；窗口结束才有
 * disappears。 */
static void test_pulse_hold_window(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    iolink_diag_pulse(&events, IOLINK_DIAG_SRC_COMM_CRC);
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_COMM_CRC), 1U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, 0x1801U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    /* 保持窗口内的重复脉冲：刷新 quiet_since，不重复 appears。 */
    g_ms = IOLINK_DIAG_PULSE_HOLD_MS / 2U;
    iolink_diag_pulse(&events, IOLINK_DIAG_SRC_COMM_CRC);
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_COMM_CRC), 2U);
    assert_int_equal(events.count, 0U);

    /* 距最后一次发生不足保持时间：条件保持。 */
    iolink_diag_service(&events, (IOLINK_DIAG_PULSE_HOLD_MS / 2U) + IOLINK_DIAG_PULSE_HOLD_MS - 1U);
    assert_int_equal(events.pending_count, 1U);

    /* 窗口结束：条件清除；disappears 仍受 50 ms 最小出现窗口约束。 */
    iolink_diag_service(&events, (IOLINK_DIAG_PULSE_HOLD_MS / 2U) + IOLINK_DIAG_PULSE_HOLD_MS);
    assert_int_equal(events.pending_count, 0U);
    g_ms = 2U * IOLINK_DIAG_PULSE_HOLD_MS;
    iolink_events_process(&events);
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, 0x1801U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* ISR 只写 one-shot；主上下文 service 时消费并产生事件（单字节发布不丢）。 */
static void test_isr_pulse_consumed_by_service(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    iolink_diag_pulse_isr(&events, IOLINK_DIAG_SRC_COMM_TIMEOUT);
    assert_int_equal(events.diag.isr_pulse[IOLINK_DIAG_SRC_COMM_TIMEOUT], 1U);
    assert_int_equal(events.count, 0U);

    iolink_diag_service(&events, 0U);
    assert_int_equal(events.diag.isr_pulse[IOLINK_DIAG_SRC_COMM_TIMEOUT], 0U);
    assert_int_equal(events.pending_count, 1U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, 0x1802U);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    /* 空 service 不产生新工作（幂等）。 */
    iolink_diag_service(&events, 1U);
    assert_int_equal(events.count, 0U);
}

/* 注册表规范化：已登记代码的调用方类型被覆盖；未登记代码保持兼容输入。 */
static void test_trigger_normalizes_registered_type(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    /* Table D.1：0x4000 是 Error，调用方传 Warning 不得让 DeviceStatus 降级。 */
    iolink_event_trigger(&events, 0x4000U, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_diag_device_status(&events), 4U);
    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);

    /* 未登记代码：保持调用方类型（应用兼容）。 */
    iolink_event_trigger(&events, 0x2222U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
}

/* DeviceStatus 与 DDS 使用同一条件集：Notification 不提升状态、不进入 DDS；
 * 条件存在但 appears 仍被 1 s 窗口推迟时状态也反映当前诊断。 */
static void test_status_and_dds_share_condition_set(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_UNDERVOLTAGE, true);
    iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT, true);
    iolink_event_trigger(&events, IOLINK_EVENTCODE_NO_MALFUNCTION, IOLINK_EVENT_TYPE_NOTIFICATION);

    /* Table B.13：Failure 优先于 Out-of-Spec。 */
    assert_int_equal(iolink_diag_device_status(&events), 4U);

    iolink_diag_dds_entry_t entries[IOLINK_EVENT_PENDING_MAX];
    const uint8_t n = iolink_diag_dds_snapshot(&events, entries, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(n, 2U);

    bool seen_under = false;
    bool seen_short = false;
    for (uint8_t i = 0U; i < n; i++) {
        assert_int_equal(
            entries[i].qualifier,
            iolink_event_qualifier(entries[i].code, iolink_event_classify(entries[i].code),
                                   IOLINK_EVENT_MODE_APPEARS));
        if (entries[i].code == 0x5111U) {
            seen_under = true;
            assert_int_equal(entries[i].qualifier, 0xE4U); /* appears | warning */
        }
        if (entries[i].code == 0x7710U) {
            seen_short = true;
            assert_int_equal(entries[i].qualifier, 0xF4U); /* appears | error */
        }
    }
    assert_true(seen_under);
    assert_true(seen_short);

    /* 部分下降只影响对应条件与状态。 */
    iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT, false);
    assert_int_equal(iolink_diag_device_status(&events), 4U); /* DDS 仍列出短路 */

    g_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&events);
    assert_int_equal(iolink_diag_device_status(&events), 2U); /* 仅剩欠压 */

    /* 快照容量边界：max=1 只返回一条。 */
    assert_int_equal(iolink_diag_dds_snapshot(&events, entries, 1U), 1U);
    assert_int_equal(iolink_diag_dds_snapshot(&events, NULL, 1U), 0U);
}

/* rearm：只清 present；仍为高电平时下一次采样产生新的上升沿并保留边沿历史。 */
static void test_rearm_rearms_level_latch(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERVOLTAGE, true);
    assert_int_equal(events.pending_count, 1U);

    iolink_diag_rearm(&events, (uint8_t) (1U << IOLINK_DIAG_SRC_PHY_OVERVOLTAGE));
    assert_int_equal(events.pending_count, 0U);
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_PHY_OVERVOLTAGE), 1U);

    /* 仍为高电平：新的上升沿（reported 尚未撤销时引擎按同一根因静默）。 */
    assert_true(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERVOLTAGE, true));
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_PHY_OVERVOLTAGE), 2U);
    assert_int_equal(events.pending_count, 1U);

    /* 非电平来源的 mask 位无副作用。 */
    iolink_diag_rearm(&events, (uint8_t) (1U << IOLINK_DIAG_SRC_COMM_CRC));
    assert_int_equal(events.pending_count, 1U);
}

/* 线上一致性：Table 58 事件内存、DDS 与 DeviceStatus 描述同一组条件。 */
static void test_wire_views_are_consistent(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP, true);
    assert_true(iolink_events_flag(&events));

    /* Table 58：StatusCode=0x81，槽位 1 = 0xF4 | 0x4000。 */
    assert_int_equal(iolink_events_memory_read(&events, 0x00U), 0x81U);
    const uint8_t qualifier = iolink_events_memory_read(&events, 0x01U);
    const uint16_t code = (uint16_t) (((uint16_t) iolink_events_memory_read(&events, 0x02U) << 8) |
                                      iolink_events_memory_read(&events, 0x03U));

    iolink_diag_dds_entry_t entries[IOLINK_EVENT_PENDING_MAX];
    const uint8_t n = iolink_diag_dds_snapshot(&events, entries, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(n, 1U);
    assert_int_equal(entries[0].code, code);
    assert_int_equal(entries[0].qualifier, qualifier);
    assert_int_equal(code, 0x4000U);
    assert_int_equal(iolink_diag_device_status(&events), 4U);

    /* 主站确认只释放事件内存，不改变条件与 DDS。 */
    iolink_events_memory_write(&events, 0x00U, 0x00U);
    assert_false(iolink_events_flag(&events));
    assert_int_equal(iolink_diag_dds_snapshot(&events, entries, IOLINK_EVENT_PENDING_MAX), 1U);
    assert_int_equal(iolink_diag_device_status(&events), 4U);
}

/* 复位：条件表与诊断视图同时归零；仍存在的故障由下一轮电平采样重新上报。 */
static void test_reset_clears_conditions_and_resamples(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT, true);
    assert_int_equal(iolink_diag_device_status(&events), 4U);

    iolink_events_reset(&events);
    assert_int_equal(iolink_diag_device_status(&events), 0U);
    assert_int_equal(iolink_diag_edge_count(&events, IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT), 0U);

    assert_true(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT, true));
    assert_int_equal(iolink_diag_device_status(&events), 4U);
}

/* Restore factory（0x82）：先撤消失效条件，再对仍存在的电平重新锁存
 * （D5：复位后不能把硬件故障的 DeviceStatus 永久留在 0）。 */
static void test_clear_diagnosis_rearms_on_next_sample(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_UNDERVOLTAGE, true);
    iolink_events_clear_diagnosis(&events);
    assert_int_equal(events.pending_count, 0U);

    /* 仍为高电平：下一次采样重新锁存（appears 受 1 s 窗口约束，但条件
       在 DeviceStatus 中已经反映）。 */
    assert_true(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_PHY_UNDERVOLTAGE, true));
    assert_int_equal(events.pending_count, 1U);
    assert_int_equal(iolink_diag_device_status(&events), 2U);
}

/* DLL 端到端：ISR 只发布 one-shot，主泵 service 后产生事件；0x0024 派生
 * 值与事件队列一致。 */
static int diag_phy_init(void* user)
{
    (void) user;
    return 0;
}

static void diag_phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void diag_phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static const iolink_phy_api_t diag_phy_no_faults = {
    .init = diag_phy_init,
    .set_mode = diag_phy_set_mode,
    .set_baudrate = diag_phy_set_baudrate,
    .detect_wakeup = NULL,
    .get_faults = NULL,
};

static void test_dll_service_publishes_isr_pulse(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx.main, &ctx.isr, &diag_phy_no_faults);

    /* ISR 观察到的组帧错误只写 one-shot，不触发事件。 */
    iolink_diag_pulse_isr(&ctx.main.shared.events, IOLINK_DIAG_SRC_COMM_FRAMING);
    assert_int_equal(ctx.main.shared.events.count, 0U);

    iolink_dll_process(&ctx.main);

    /* 主泵 service 后事件上线，DeviceStatus 由注册表（0x1803 = Error）派生。 */
    assert_int_equal(ctx.main.shared.events.pending_count, 1U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx.main.shared.events), 4U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.main.shared.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_FRAMING);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
}

/* 防御性 API 边界：NULL/越界/来源类型错误的调用不改变任何状态。 */
static void test_api_guards(void** state)
{
    (void) state;
    iolink_events_ctx_t events;
    diag_events_init(&events);

    assert_false(iolink_diag_set_level(NULL, IOLINK_DIAG_SRC_PHY_OVERTEMP, true));
    assert_false(iolink_diag_set_level(&events, (iolink_diag_src_t) IOLINK_DIAG_SRC_COUNT, true));
    assert_false(iolink_diag_set_level(&events, IOLINK_DIAG_SRC_COMM_CRC, true));

    iolink_diag_pulse(NULL, IOLINK_DIAG_SRC_COMM_CRC);
    iolink_diag_pulse(&events, IOLINK_DIAG_SRC_PHY_OVERTEMP);
    iolink_diag_pulse_isr(NULL, IOLINK_DIAG_SRC_COMM_CRC);
    iolink_diag_pulse_isr(&events, (iolink_diag_src_t) IOLINK_DIAG_SRC_COUNT);
    assert_int_equal(events.count, 0U);

    iolink_diag_service(NULL, 0U);
    iolink_diag_rearm(NULL, 0xFFU);
    iolink_diag_rearm(&events, 0xFFU); /* 无活动条件：无副作用 */

    iolink_diag_raise_code(NULL, 0x1000U);
    iolink_diag_clear_code(NULL, 0x1000U);

    assert_int_equal(iolink_diag_device_status(NULL), 0U);
    iolink_diag_dds_entry_t entry;
    assert_int_equal(iolink_diag_dds_snapshot(NULL, &entry, 1U), 0U);
    assert_int_equal(iolink_diag_dds_snapshot(&events, &entry, 0U), 0U);
    assert_int_equal(iolink_diag_edge_count(NULL, IOLINK_DIAG_SRC_COMM_CRC), 0U);
    assert_int_equal(iolink_diag_edge_count(&events, (iolink_diag_src_t) IOLINK_DIAG_SRC_COUNT),
                     0U);
    assert_int_equal(iolink_diag_src_code((iolink_diag_src_t) IOLINK_DIAG_SRC_COUNT), 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_registry_matches_table_d1),
        cmocka_unit_test(test_level_source_edges),
        cmocka_unit_test(test_pulse_hold_window),
        cmocka_unit_test(test_isr_pulse_consumed_by_service),
        cmocka_unit_test(test_trigger_normalizes_registered_type),
        cmocka_unit_test(test_status_and_dds_share_condition_set),
        cmocka_unit_test(test_rearm_rearms_level_latch),
        cmocka_unit_test(test_wire_views_are_consistent),
        cmocka_unit_test(test_reset_clears_conditions_and_resamples),
        cmocka_unit_test(test_clear_diagnosis_rearms_on_next_sample),
        cmocka_unit_test(test_dll_service_publishes_isr_pulse),
        cmocka_unit_test(test_api_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
